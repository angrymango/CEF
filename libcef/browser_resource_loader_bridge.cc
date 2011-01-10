// Copyright (c) 2010 The Chromium Embedded Framework Authors.
// Portions copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file contains an implementation of the ResourceLoaderBridge class.
// The class is implemented using net::URLRequest, meaning it is a "simple"
// version that directly issues requests. The more complicated one used in the
// browser uses IPC.
//
// Because net::URLRequest only provides an asynchronous resource loading API,
// this file makes use of net::URLRequest from a background IO thread.  Requests
// for cookies and synchronously loaded resources result in the main thread of
// the application blocking until the IO thread completes the operation.  (See
// GetCookies and SyncLoad)
//
// Main thread                          IO thread
// -----------                          ---------
// ResourceLoaderBridge <---o---------> RequestProxy (normal case)
//                           \            -> net::URLRequest
//                            o-------> SyncRequestProxy (synchronous case)
//                                        -> net::URLRequest
// SetCookie <------------------------> CookieSetter
//                                        -> net_util::SetCookie
// GetCookies <-----------------------> CookieGetter
//                                        -> net_util::GetCookies
//
// NOTE: The implementation in this file may be used to have WebKit fetch
// resources in-process.  For example, it is handy for building a single-
// process WebKit embedding (e.g., test_shell) that can use net::URLRequest to
// perform URL loads.  See renderer/resource_dispatcher.h for details on an
// alternate implementation that defers fetching to another process.

#include "browser_appcache_system.h"
#include "browser_resource_loader_bridge.h"
#include "browser_request_context.h"
#include "browser_socket_stream_bridge.h"
#include "browser_webkit_glue.h"
#include "browser_impl.h"
#include "cef_context.h"
#include "cef_process.h"
#include "cef_process_io_thread.h"
#include "request_impl.h"

#include "base/file_path.h"
#include "base/file_util.h"
#include "base/message_loop.h"
#include "base/message_loop_proxy.h"
#if defined(OS_MACOSX) || defined(OS_WIN)
#include "base/nss_util.h"
#endif
#include "base/ref_counted.h"
#include "base/time.h"
#include "base/timer.h"
#include "base/threading/thread.h"
#include "base/synchronization/waitable_event.h"
#include "net/base/auth.h"
#include "net/base/cookie_store.h"
#include "net/base/file_stream.h"
#include "net/base/io_buffer.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/base/net_util.h"
#include "net/base/static_cookie_policy.h"
#include "net/base/upload_data.h"
#include "net/http/http_cache.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_response_headers.h"
#include "net/proxy/proxy_service.h"
#if defined(OS_WIN)
#include "net/socket/ssl_client_socket_nss_factory.h"
#endif
#include "net/url_request/url_request.h"
#include "webkit/appcache/appcache_interfaces.h"
#include "webkit/blob/blob_storage_controller.h"
#include "webkit/blob/deletable_file_reference.h"
#include "webkit/glue/resource_loader_bridge.h"

using net::HttpResponseHeaders;
using net::StaticCookiePolicy;
using webkit_blob::DeletableFileReference;
using webkit_glue::ResourceLoaderBridge;
using webkit_glue::ResourceResponseInfo;


namespace {

struct RequestParams {
  std::string method;
  GURL url;
  GURL first_party_for_cookies;
  GURL referrer;
  std::string headers;
  int load_flags;
  ResourceType::Type request_type;
  int appcache_host_id;
  bool download_to_file;
  scoped_refptr<net::UploadData> upload;
};

// The interval for calls to RequestProxy::MaybeUpdateUploadProgress
static const int kUpdateUploadProgressIntervalMsec = 100;

class ExtraRequestInfo : public net::URLRequest::UserData {
public:
  ExtraRequestInfo(ResourceType::Type resource_type)
    : resource_type_(resource_type),
      allow_download_(resource_type == ResourceType::MAIN_FRAME || 
                      resource_type == ResourceType::SUB_FRAME)
  { }

  // Identifies the type of resource, such as subframe, media, etc.
  ResourceType::Type resource_type() const { return resource_type_; }
  bool allow_download() const { return allow_download_; }

private:
  ResourceType::Type resource_type_;
  bool allow_download_;
};

// The RequestProxy does most of its work on the IO thread.  The Start and
// Cancel methods are proxied over to the IO thread, where an net::URLRequest
// object is instantiated.
class RequestProxy : public net::URLRequest::Delegate,
                     public base::RefCountedThreadSafe<RequestProxy> {
 public:
  // Takes ownership of the params.
  RequestProxy(CefRefPtr<CefBrowser> browser)
    : download_to_file_(false),
      buf_(new net::IOBuffer(kDataSize)),
      browser_(browser),
      last_upload_position_(0)
  {
  }

  void DropPeer() {
    peer_ = NULL;
  }

  void Start(ResourceLoaderBridge::Peer* peer, RequestParams* params) {
    peer_ = peer;
    owner_loop_ = MessageLoop::current();

    // proxy over to the io thread
    CefThread::PostTask(CefThread::IO, FROM_HERE, NewRunnableMethod(
        this, &RequestProxy::AsyncStart, params));
  }

  void Cancel() {
    if(download_handler_.get()) {
      // WebKit will try to cancel the download but we won't allow it.
      return;
    }

    // proxy over to the io thread
    CefThread::PostTask(CefThread::IO, FROM_HERE, NewRunnableMethod(
        this, &RequestProxy::AsyncCancel));
  }

 protected:
  friend class base::RefCountedThreadSafe<RequestProxy>;

  virtual ~RequestProxy() {
    // If we have a request, then we'd better be on the io thread!
    DCHECK(!request_.get() || CefThread::CurrentlyOn(CefThread::IO));
  }

  // --------------------------------------------------------------------------
  // The following methods are called on the owner's thread in response to
  // various net::URLRequest callbacks.  The event hooks, defined below, trigger
  // these methods asynchronously.

  void NotifyReceivedRedirect(const GURL& new_url,
                              const ResourceResponseInfo& info) {
    bool has_new_first_party_for_cookies = false;
    GURL new_first_party_for_cookies;
    if (peer_ && peer_->OnReceivedRedirect(new_url, info,
                                           &has_new_first_party_for_cookies,
                                           &new_first_party_for_cookies)) {
      CefThread::PostTask(CefThread::IO, FROM_HERE, NewRunnableMethod(
          this, &RequestProxy::AsyncFollowDeferredRedirect,
          has_new_first_party_for_cookies, new_first_party_for_cookies));
    } else {
      Cancel();
    }
  }

  void NotifyReceivedResponse(const ResourceResponseInfo& info,
                              bool content_filtered,
                              const GURL& url, bool allow_download) {

    if (browser_.get() && info.headers.get()) {
      CefRefPtr<CefHandler> handler = browser_->GetHandler();
      if (handler.get()) {
        std::string content_disposition;
        info.headers->GetNormalizedHeader("Content-Disposition",
            &content_disposition);

        if (allow_download &&
            webkit_glue::ShouldDownload(content_disposition, info.mime_type)) {
          FilePath path(net::GetSuggestedFilename(url, content_disposition,
              info.charset, FilePath(FILE_PATH_LITERAL("download"))));
          CefRefPtr<CefDownloadHandler> dl_handler;
          if (handler->HandleDownloadResponse(browser_, info.mime_type,
                  path.value(), info.content_length, dl_handler) ==
                  RV_CONTINUE) {
            download_handler_ = dl_handler;
          }
        }
      }
    }

    if (peer_)
      peer_->OnReceivedResponse(info, content_filtered);
  }

  void NotifyReceivedData(int bytes_read) {
    if (!peer_)
      return;

    // Make a local copy of buf_, since AsyncReadData reuses it.
    scoped_array<char> buf_copy(new char[bytes_read]);
    memcpy(buf_copy.get(), buf_->data(), bytes_read);

    // Continue reading more data into buf_
    // Note: Doing this before notifying our peer ensures our load events get
    // dispatched in a manner consistent with DumpRenderTree (and also avoids a
    // race condition).  If the order of the next 2 functions were reversed, the
    // peer could generate new requests in reponse to the received data, which
    // when run on the io thread, could race against this function in doing
    // another InvokeLater.  See bug 769249.
    CefThread::PostTask(CefThread::IO, FROM_HERE, NewRunnableMethod(
        this, &RequestProxy::AsyncReadData));

    if (download_handler_.get() &&
        !download_handler_->ReceivedData(buf_copy.get(), bytes_read)) {
      // Cancel loading by proxying over to the io thread.
      CefThread::PostTask(CefThread::IO, FROM_HERE, NewRunnableMethod(
          this, &RequestProxy::AsyncCancel));
    }

    peer_->OnReceivedData(buf_copy.get(), bytes_read);
  }

  void NotifyDownloadedData(int bytes_read) {
    if (!peer_)
      return;

    // Continue reading more data, see the comment in NotifyReceivedData.
    CefThread::PostTask(CefThread::IO, FROM_HERE, NewRunnableMethod(
        this, &RequestProxy::AsyncReadData));

    peer_->OnDownloadedData(bytes_read);
  }

  void NotifyCompletedRequest(const URLRequestStatus& status,
                              const std::string& security_info,
                              const base::Time& complete_time) {
    if (download_handler_.get()) {
      download_handler_->Complete();
      download_handler_ = NULL;
    }

    if (peer_) {
      peer_->OnCompletedRequest(status, security_info, complete_time);
      DropPeer();  // ensure no further notifications
    }
  }

  void NotifyUploadProgress(uint64 position, uint64 size) {
    if (peer_)
      peer_->OnUploadProgress(position, size);
  }

  // --------------------------------------------------------------------------
  // The following methods are called on the io thread.  They correspond to
  // actions performed on the owner's thread.

  void AsyncStart(RequestParams* params) {
    bool handled = false;

    if (browser_.get()) {
      CefRefPtr<CefHandler> handler = browser_->GetHandler();
      if(handler.get())
      {
        // Build the request object for passing to the handler
        CefRefPtr<CefRequest> request(new CefRequestImpl());
        CefRequestImpl* requestimpl =
            static_cast<CefRequestImpl*>(request.get());

        std::string originalUrl(params->url.spec());
        requestimpl->SetURL(originalUrl);
        requestimpl->SetMethod(params->method);
        
        // Transfer request headers
        CefRequest::HeaderMap headerMap;
        CefRequestImpl::ParseHeaders(params->headers, headerMap);
        headerMap.insert(std::make_pair("Referrer", params->referrer.spec()));
        requestimpl->SetHeaderMap(headerMap);

        // Transfer post data, if any
        scoped_refptr<net::UploadData> upload = params->upload;
        if(upload.get()) {
          CefRefPtr<CefPostData> postdata(new CefPostDataImpl());
          static_cast<CefPostDataImpl*>(postdata.get())->Set(*upload.get());
          requestimpl->SetPostData(postdata);
        }

        int loadFlags = params->load_flags;

        // Handler output will be returned in these variables
        CefString redirectUrl;
        CefRefPtr<CefStreamReader> resourceStream;
        CefString mimeType;
        
        CefHandler::RetVal rv = handler->HandleBeforeResourceLoad(
            browser_, request, redirectUrl, resourceStream, mimeType,
            loadFlags);

        // Observe URL from request.
        const std::string requestUrl(request->GetURL());
        if(requestUrl != originalUrl) {
          params->url = GURL(requestUrl);
          redirectUrl.clear(); // Request URL trumps redirect URL
        }

        // Observe method from request.
        params->method = request->GetMethod();

        // Observe headers from request.
        request->GetHeaderMap(headerMap);
        CefString referrerStr;
        referrerStr.FromASCII("Referrer");
        CefRequest::HeaderMap::iterator referrer = headerMap.find(referrerStr);
        if(referrer == headerMap.end()) {
          params->referrer = GURL();
        } else {
          params->referrer = GURL(std::string(referrer->second));
          headerMap.erase(referrer);
        }
        params->headers = CefRequestImpl::GenerateHeaders(headerMap);

        // Observe post data from request.
        CefRefPtr<CefPostData> postData = request->GetPostData();
        if(postData.get()) {
          params->upload = new net::UploadData();
          static_cast<CefPostDataImpl*>(postData.get())->Get(*params->upload);
        }

        if(rv == RV_HANDLED) {
          // cancel the resource load
          handled = true;
          OnCompletedRequest(
              URLRequestStatus(URLRequestStatus::CANCELED, net::ERR_ABORTED),
              std::string(), base::Time());
        } else if(!redirectUrl.empty()) {
          // redirect to the specified URL
          params->url = GURL(std::string(redirectUrl));
          ResourceResponseInfo info;
          bool defer_redirect;
          OnReceivedRedirect(params->url, info, &defer_redirect);
        } else if(resourceStream.get()) {
          // load from the provided resource stream
          handled = true;

          resourceStream->Seek(0, SEEK_END);
          long offset = resourceStream->Tell();
          resourceStream->Seek(0, SEEK_SET);

          resource_stream_ = resourceStream;

          ResourceResponseInfo info;
          info.content_length = static_cast<int64>(offset);
          if(!mimeType.empty())
            info.mime_type = mimeType;
          OnReceivedResponse(info, false);
          AsyncReadData();
        }
      }
    }

    if(!handled)
    {
      // Might need to resolve the blob references in the upload data.
      if (params->upload) {
        _Context->request_context()->blob_storage_controller()->
            ResolveBlobReferencesInUploadData(params->upload.get());
      }

      request_.reset(new net::URLRequest(params->url, this));
      request_->set_method(params->method);
      request_->set_first_party_for_cookies(params->first_party_for_cookies);
      request_->set_referrer(params->referrer.spec());
      net::HttpRequestHeaders headers;
      headers.AddHeadersFromString(params->headers);
      request_->SetExtraRequestHeaders(headers);
      request_->set_load_flags(params->load_flags);
      request_->set_upload(params->upload.get());
      request_->set_context(_Context->request_context());
      request_->SetUserData(NULL, new ExtraRequestInfo(params->request_type));
      BrowserAppCacheSystem::SetExtraRequestInfo(
          request_.get(), params->appcache_host_id, params->request_type);

      download_to_file_ = params->download_to_file;
      if (download_to_file_) {
        FilePath path;
        if (file_util::CreateTemporaryFile(&path)) {
          downloaded_file_ = DeletableFileReference::GetOrCreate(
              path, base::MessageLoopProxy::CreateForCurrentThread());
          file_stream_.Open(
              path, base::PLATFORM_FILE_OPEN | base::PLATFORM_FILE_WRITE);
        }
      }

      request_->Start();

      if (request_->has_upload() &&
          params->load_flags & net::LOAD_ENABLE_UPLOAD_PROGRESS) {
        upload_progress_timer_.Start(
            base::TimeDelta::FromMilliseconds(
                kUpdateUploadProgressIntervalMsec),
            this, &RequestProxy::MaybeUpdateUploadProgress);
      }
    }

    delete params;
  }

  void AsyncCancel() {
    // This can be null in cases where the request is already done.
    if (!resource_stream_.get() && !request_.get())
      return;

    if (request_.get())
      request_->Cancel();
    Done();
  }

  void AsyncFollowDeferredRedirect(bool has_new_first_party_for_cookies,
                                   const GURL& new_first_party_for_cookies) {
    // This can be null in cases where the request is already done.
    if (!request_.get())
      return;

    if (has_new_first_party_for_cookies)
      request_->set_first_party_for_cookies(new_first_party_for_cookies);
    request_->FollowDeferredRedirect();
  }

  void AsyncReadData() {
    if(resource_stream_.get()) {
      // Read from the handler-provided resource stream
      int bytes_read = resource_stream_->Read(buf_->data(), 1, kDataSize);
      if(bytes_read > 0) {
        OnReceivedData(bytes_read);
      } else {
        Done();
      }
      return;
    }

    // This can be null in cases where the request is already done.
    if (!request_.get())
      return;

    if (request_->status().is_success()) {
      int bytes_read;
      if (request_->Read(buf_, kDataSize, &bytes_read) && bytes_read) {
        OnReceivedData(bytes_read);
      } else if (!request_->status().is_io_pending()) {
        Done();
      }  // else wait for OnReadCompleted
    } else {
      Done();
    }
  }

  // --------------------------------------------------------------------------
  // The following methods are event hooks (corresponding to net::URLRequest
  // callbacks) that run on the IO thread.  They are designed to be overridden
  // by the SyncRequestProxy subclass.

  virtual void OnReceivedRedirect(
      const GURL& new_url,
      const ResourceResponseInfo& info,
      bool* defer_redirect) {
    *defer_redirect = true;  // See AsyncFollowDeferredRedirect
    owner_loop_->PostTask(FROM_HERE, NewRunnableMethod(
        this, &RequestProxy::NotifyReceivedRedirect, new_url, info));
  }

  virtual void OnReceivedResponse(
      const ResourceResponseInfo& info,
      bool content_filtered) {
    GURL url;
    bool allow_download(false);
    if (request_.get()){
      url = request_->url();
      ExtraRequestInfo* info =
          static_cast<ExtraRequestInfo*>(request_->GetUserData(NULL));
      if (info)
        allow_download = info->allow_download();
    }

    owner_loop_->PostTask(FROM_HERE, NewRunnableMethod(
        this, &RequestProxy::NotifyReceivedResponse, info, content_filtered,
        url, allow_download));
  }

  virtual void OnReceivedData(int bytes_read) {
    if (download_to_file_) {
      file_stream_.Write(buf_->data(), bytes_read, NULL);
      owner_loop_->PostTask(FROM_HERE, NewRunnableMethod(
          this, &RequestProxy::NotifyDownloadedData, bytes_read));
      return;
    }
    
    owner_loop_->PostTask(FROM_HERE, NewRunnableMethod(
        this, &RequestProxy::NotifyReceivedData, bytes_read));
  }

  virtual void OnCompletedRequest(const URLRequestStatus& status,
                                  const std::string& security_info,
                                  const base::Time& complete_time) {
    if (download_to_file_)
      file_stream_.Close();

    owner_loop_->PostTask(FROM_HERE, NewRunnableMethod(
        this, &RequestProxy::NotifyCompletedRequest, status, security_info,
        complete_time));
  }

  // --------------------------------------------------------------------------
  // net::URLRequest::Delegate implementation:

  virtual void OnReceivedRedirect(net::URLRequest* request,
                                  const GURL& new_url,
                                  bool* defer_redirect) {
    DCHECK(request->status().is_success());
    ResourceResponseInfo info;
    PopulateResponseInfo(request, &info);
    OnReceivedRedirect(new_url, info, defer_redirect);
  }

  virtual void OnResponseStarted(net::URLRequest* request) {
    if (request->status().is_success()) {
      ResourceResponseInfo info;
      PopulateResponseInfo(request, &info);
      OnReceivedResponse(info, false);
      AsyncReadData();  // start reading
    } else {
      Done();
    }
  }

  virtual void OnAuthRequired(net::URLRequest* request,
                              net::AuthChallengeInfo* auth_info) {
    if (browser_.get()) {
      CefRefPtr<CefHandler> handler = browser_->GetHandler();
      if(handler.get()) {
        CefString username, password;
        CefHandler::RetVal rv = handler->HandleAuthenticationRequest(
            browser_, auth_info->is_proxy,
            auth_info->host_and_port, auth_info->realm,
            auth_info->scheme, username, password);
        if (rv == RV_HANDLED) {
          request->SetAuth(username, password);
          return;
        }
      }
    }

    request->CancelAuth();
  }

  virtual void OnSSLCertificateError(net::URLRequest* request,
                                     int cert_error,
                                     net::X509Certificate* cert) {
    // Allow all certificate errors.
    request->ContinueDespiteLastError();
  }

  virtual void OnReadCompleted(net::URLRequest* request, int bytes_read) {
    if (request->status().is_success() && bytes_read > 0) {
      OnReceivedData(bytes_read);
    } else {
      Done();
    }
  }

  // --------------------------------------------------------------------------
  // Helpers and data:

  void Done() {
    if(resource_stream_.get()) {
      // Resource stream reads always complete successfully
      OnCompletedRequest(URLRequestStatus(URLRequestStatus::SUCCESS, 0),
          std::string(), base::Time());
      resource_stream_ = NULL;
    } else if(request_.get()) {
      if (upload_progress_timer_.IsRunning()) {
        MaybeUpdateUploadProgress();
        upload_progress_timer_.Stop();
      }
      DCHECK(request_.get());
      OnCompletedRequest(request_->status(), std::string(), base::Time());
      request_.reset();  // destroy on the io thread
    }
  }

  // Called on the IO thread.
  void MaybeUpdateUploadProgress() {
    // If a redirect is received upload is cancelled in net::URLRequest, we
    // should try to stop the |upload_progress_timer_| timer and return.
    if (!request_->has_upload()) {
      if (upload_progress_timer_.IsRunning())
        upload_progress_timer_.Stop();
      return;
    }

    uint64 size = request_->get_upload()->GetContentLength();
    uint64 position = request_->GetUploadProgress();
    if (position == last_upload_position_)
      return;  // no progress made since last time

    const uint64 kHalfPercentIncrements = 200;
    const base::TimeDelta kOneSecond = base::TimeDelta::FromMilliseconds(1000);

    uint64 amt_since_last = position - last_upload_position_;
    base::TimeDelta time_since_last = base::TimeTicks::Now() -
                                      last_upload_ticks_;

    bool is_finished = (size == position);
    bool enough_new_progress = (amt_since_last > (size /
                                                  kHalfPercentIncrements));
    bool too_much_time_passed = time_since_last > kOneSecond;

    if (is_finished || enough_new_progress || too_much_time_passed) {
      owner_loop_->PostTask(FROM_HERE, NewRunnableMethod(
          this, &RequestProxy::NotifyUploadProgress, position, size));
      last_upload_ticks_ = base::TimeTicks::Now();
      last_upload_position_ = position;
    }
  }

  void PopulateResponseInfo(net::URLRequest* request,
                            ResourceResponseInfo* info) const {
    info->request_time = request->request_time();
    info->response_time = request->response_time();
    info->headers = request->response_headers();
    request->GetMimeType(&info->mime_type);
    request->GetCharset(&info->charset);
    info->content_length = request->GetExpectedContentSize();
    if (downloaded_file_)
      info->download_file_path = downloaded_file_->path();
    BrowserAppCacheSystem::GetExtraResponseInfo(
        request,
        &info->appcache_id,
        &info->appcache_manifest_url);
  }

  scoped_ptr<net::URLRequest> request_;
  CefRefPtr<CefStreamReader> resource_stream_;

  // Support for request.download_to_file behavior.
  bool download_to_file_;
  net::FileStream file_stream_;
  scoped_refptr<DeletableFileReference> downloaded_file_;

  // Size of our async IO data buffers
  static const int kDataSize = 16*1024;

  // read buffer for async IO
  scoped_refptr<net::IOBuffer> buf_;

  CefRefPtr<CefBrowser> browser_;

  MessageLoop* owner_loop_;

  // This is our peer in WebKit (implemented as ResourceHandleInternal). We do
  // not manage its lifetime, and we may only access it from the owner's
  // message loop (owner_loop_).
  ResourceLoaderBridge::Peer* peer_;

  // Timer used to pull upload progress info.
  base::RepeatingTimer<RequestProxy> upload_progress_timer_;

  // Info used to determine whether or not to send an upload progress update.
  uint64 last_upload_position_;
  base::TimeTicks last_upload_ticks_;

  CefRefPtr<CefDownloadHandler> download_handler_;
};

//-----------------------------------------------------------------------------

class SyncRequestProxy : public RequestProxy {
 public:
  explicit SyncRequestProxy(CefRefPtr<CefBrowser> browser,
                            ResourceLoaderBridge::SyncLoadResponse* result)
      : RequestProxy(browser), result_(result), event_(true, false) {
  }

  void WaitForCompletion() {
    if (!event_.Wait())
      NOTREACHED();
  }

  // --------------------------------------------------------------------------
  // Event hooks that run on the IO thread:

  virtual void OnReceivedRedirect(
      const GURL& new_url,
      const ResourceResponseInfo& info,
      bool* defer_redirect) {
    // TODO(darin): It would be much better if this could live in WebCore, but
    // doing so requires API changes at all levels.  Similar code exists in
    // WebCore/platform/network/cf/ResourceHandleCFNet.cpp :-(
    if (new_url.GetOrigin() != result_->url.GetOrigin()) {
      DLOG(WARNING) << "Cross origin redirect denied";
      Cancel();
      return;
    }
    result_->url = new_url;
  }

  virtual void OnReceivedResponse(
      const ResourceResponseInfo& info,
      bool content_filtered) {
    *static_cast<ResourceResponseInfo*>(result_) = info;
  }

  virtual void OnReceivedData(int bytes_read) {
    if (download_to_file_)
      file_stream_.Write(buf_->data(), bytes_read, NULL);
    else
      result_->data.append(buf_->data(), bytes_read);
    AsyncReadData();  // read more (may recurse)
  }

  virtual void OnCompletedRequest(const URLRequestStatus& status,
                                  const std::string& security_info,
                                  const base::Time& complete_time) {
    if (download_to_file_)
      file_stream_.Close();
    
    result_->status = status;
    event_.Signal();
  }

 private:
  ResourceLoaderBridge::SyncLoadResponse* result_;
  base::WaitableEvent event_;
};

//-----------------------------------------------------------------------------

class ResourceLoaderBridgeImpl : public ResourceLoaderBridge {
 public:
  ResourceLoaderBridgeImpl(CefRefPtr<CefBrowser> browser,
      const webkit_glue::ResourceLoaderBridge::RequestInfo& request_info)
      : browser_(browser),
        params_(new RequestParams),
        proxy_(NULL) {
    params_->method = request_info.method;
    params_->url = request_info.url;
    params_->first_party_for_cookies = request_info.first_party_for_cookies;
    params_->referrer = request_info.referrer;
    params_->headers = request_info.headers;
    params_->load_flags = request_info.load_flags;
    params_->request_type = request_info.request_type;
    params_->appcache_host_id = request_info.appcache_host_id;
    params_->download_to_file = request_info.download_to_file;
  }

  virtual ~ResourceLoaderBridgeImpl() {
    if (proxy_) {
      proxy_->DropPeer();
      // Let the proxy die on the IO thread
      CefThread::ReleaseSoon(CefThread::IO, FROM_HERE, proxy_);
    }
  }

  // --------------------------------------------------------------------------
  // ResourceLoaderBridge implementation:

  virtual void AppendDataToUpload(const char* data, int data_len) {
    DCHECK(params_.get());
    if (!params_->upload)
      params_->upload = new net::UploadData();
    params_->upload->AppendBytes(data, data_len);
  }

  virtual void AppendFileRangeToUpload(
      const FilePath& file_path,
      uint64 offset,
      uint64 length,
      const base::Time& expected_modification_time) {
    DCHECK(params_.get());
    if (!params_->upload)
      params_->upload = new net::UploadData();
    params_->upload->AppendFileRange(file_path, offset, length,
                                     expected_modification_time);
  }

  virtual void AppendBlobToUpload(const GURL& blob_url) {
    DCHECK(params_.get());
    if (!params_->upload)
      params_->upload = new net::UploadData();
    params_->upload->AppendBlob(blob_url);
  }

  virtual void SetUploadIdentifier(int64 identifier) {
    DCHECK(params_.get());
    if (!params_->upload)
      params_->upload = new net::UploadData();
    params_->upload->set_identifier(identifier);
  }

  virtual bool Start(Peer* peer) {
    DCHECK(!proxy_);

    proxy_ = new RequestProxy(browser_);
    proxy_->AddRef();

    proxy_->Start(peer, params_.release());

    return true;  // Any errors will be reported asynchronously.
  }

  virtual void Cancel() {
    DCHECK(proxy_);
    proxy_->Cancel();
  }

  virtual void SetDefersLoading(bool value) {
    // TODO(darin): implement me
  }

  virtual void SyncLoad(SyncLoadResponse* response) {
    DCHECK(!proxy_);

    // this may change as the result of a redirect
    response->url = params_->url;

    proxy_ = new SyncRequestProxy(browser_, response);
    proxy_->AddRef();

    proxy_->Start(NULL, params_.release());

    static_cast<SyncRequestProxy*>(proxy_)->WaitForCompletion();
  }

 private:
  CefRefPtr<CefBrowser> browser_;

  // Ownership of params_ is transfered to the proxy when the proxy is created.
  scoped_ptr<RequestParams> params_;

  // The request proxy is allocated when we start the request, and then it
  // sticks around until this ResourceLoaderBridge is destroyed.
  RequestProxy* proxy_;
};

//-----------------------------------------------------------------------------

class CookieSetter : public base::RefCountedThreadSafe<CookieSetter> {
 public:
  void Set(const GURL& url, const std::string& cookie) {
    REQUIRE_IOT();
    _Context->request_context()->cookie_store()->SetCookie(url, cookie);
  }

 private:
  friend class base::RefCountedThreadSafe<CookieSetter>;

  ~CookieSetter() {}
};

class CookieGetter : public base::RefCountedThreadSafe<CookieGetter> {
 public:
  CookieGetter() : event_(false, false) {
  }

  void Get(const GURL& url) {
    result_ = _Context->request_context()->cookie_store()->GetCookies(url);
    event_.Signal();
  }

  std::string GetResult() {
    if (!event_.Wait())
      NOTREACHED();
    return result_;
  }

 private:
  friend class base::RefCountedThreadSafe<CookieGetter>;

  ~CookieGetter() {}

  base::WaitableEvent event_;
  std::string result_;
};

}  // anonymous namespace

//-----------------------------------------------------------------------------

namespace webkit_glue {

// Factory function.
ResourceLoaderBridge* ResourceLoaderBridge::Create(
    const webkit_glue::ResourceLoaderBridge::RequestInfo& request_info) {
  CefRefPtr<CefBrowserImpl> browser =
      _Context->GetBrowserByID(request_info.routing_id);
  return new ResourceLoaderBridgeImpl(browser.get(), request_info);
}

// Issue the proxy resolve request on the io thread, and wait 
// for the result.
bool FindProxyForUrl(const GURL& url, std::string* proxy_list) {
  DCHECK(_Context->request_context());

  scoped_refptr<net::SyncProxyServiceHelper> sync_proxy_service(
      new net::SyncProxyServiceHelper(
      _Context->process()->io_thread()->message_loop(),
      _Context->request_context()->proxy_service()));

  net::ProxyInfo proxy_info;
  int rv = sync_proxy_service->ResolveProxy(url, &proxy_info,
                                            net::BoundNetLog());
  if (rv == net::OK) {
    *proxy_list = proxy_info.ToPacString();
  }

  return rv == net::OK;
}

}  // namespace webkit_glue

//-----------------------------------------------------------------------------

// static
void BrowserResourceLoaderBridge::SetCookie(const GURL& url,
                                           const GURL& first_party_for_cookies,
                                           const std::string& cookie) {
  // Proxy to IO thread to synchronize w/ network loading.

  scoped_refptr<CookieSetter> cookie_setter = new CookieSetter();
  CefThread::PostTask(CefThread::IO, FROM_HERE, NewRunnableMethod(
      cookie_setter.get(), &CookieSetter::Set, url, cookie));
}

// static
std::string BrowserResourceLoaderBridge::GetCookies(
    const GURL& url, const GURL& first_party_for_cookies) {
  // Proxy to IO thread to synchronize w/ network loading

  scoped_refptr<CookieGetter> getter = new CookieGetter();

  CefThread::PostTask(CefThread::IO, FROM_HERE, NewRunnableMethod(
      getter.get(), &CookieGetter::Get, url));

  return getter->GetResult();
}

// static
void BrowserResourceLoaderBridge::SetAcceptAllCookies(bool accept_all_cookies) {
  // Proxy to IO thread to synchronize w/ network loading
  CefThread::PostTask(CefThread::IO, FROM_HERE, NewRunnableMethod(
      _Context->request_context().get(),
      &BrowserRequestContext::SetAcceptAllCookies, accept_all_cookies));
}

//static
scoped_refptr<base::MessageLoopProxy>
    BrowserResourceLoaderBridge::GetCacheThread() {
  return CefThread::GetMessageLoopProxyForThread(CefThread::FILE);
}
