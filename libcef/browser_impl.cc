// Copyright (c) 2008-2009 The Chromium Embedded Framework Authors.
// Portions copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cef_context.h"
#include "browser_impl.h"
#include "browser_webkit_glue.h"
#include "browser_zoom_map.h"
#include "dom_document_impl.h"
#include "request_impl.h"
#include "stream_impl.h"

#include "base/file_path.h"
#include "base/path_service.h"
#include "base/synchronization/waitable_event.h"
#include "base/utf_string_conversions.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebDocument.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFrame.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebHTTPBody.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebPlugin.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebPluginDocument.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebRange.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebRect.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebScriptSource.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebString.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebURL.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebURLRequest.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebView.h"
#include "webkit/glue/glue_serialize.h"
#include "webkit/plugins/npapi/webplugin_delegate.h"
#include "webkit/plugins/npapi/webplugin_impl.h"
#include "webkit/glue/webkit_glue.h"

using WebKit::WebDocument;
using WebKit::WebFrame;
using WebKit::WebHTTPBody;
using WebKit::WebPlugin;
using WebKit::WebPluginDocument;
using WebKit::WebRange;
using WebKit::WebRect;
using WebKit::WebScriptSource;
using WebKit::WebString;
using WebKit::WebURL;
using WebKit::WebURLRequest;
using WebKit::WebView;

namespace {

class CreateBrowserHelper
{
public:
  CreateBrowserHelper(CefWindowInfo& windowInfo, bool popup,
                      CefRefPtr<CefHandler> handler, const CefString& url)
                      : window_info_(windowInfo), popup_(popup),
                        handler_(handler), url_(url) {}

  CefWindowInfo window_info_;
  bool popup_;
  CefRefPtr<CefHandler> handler_;
  CefString url_;
};

void UIT_CreateBrowserWithHelper(CreateBrowserHelper* helper)
{
  CefBrowser::CreateBrowserSync(helper->window_info_, helper->popup_,
    helper->handler_, helper->url_);
  delete helper;
}

} // namespace


CefBrowserImpl::PaintDelegate::PaintDelegate(CefBrowserImpl* browser)
  : browser_(browser)
{
}
CefBrowserImpl::PaintDelegate::~PaintDelegate()
{
}

void CefBrowserImpl::PaintDelegate::Paint(bool popup,
                                          const gfx::Rect& dirtyRect,
                                          const void* buffer)
{
  CefRefPtr<CefHandler> handler = browser_->GetHandler();
  if (!handler.get())
    return;

  CefRect rect(dirtyRect.x(), dirtyRect.y(), dirtyRect.width(),
               dirtyRect.height());
  handler->HandlePaint(browser_, (popup?PET_POPUP:PET_VIEW), rect, buffer);
}


// static
bool CefBrowser::CreateBrowser(CefWindowInfo& windowInfo, bool popup,
                               CefRefPtr<CefHandler> handler,
                               const CefString& url)
{
  // Verify that the context is in a valid state.
  if (!CONTEXT_STATE_VALID()) {
    NOTREACHED();
    return false;
  }

  // Create the browser on the UI thread.
  CreateBrowserHelper* helper =
      new CreateBrowserHelper(windowInfo, popup, handler, url);
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableFunction(
      UIT_CreateBrowserWithHelper, helper));
  return true;
}

// static
CefRefPtr<CefBrowser> CefBrowser::CreateBrowserSync(CefWindowInfo& windowInfo,
    bool popup, CefRefPtr<CefHandler> handler, const CefString& url)
{
  // Verify that the context is in a valid state.
  if (!CONTEXT_STATE_VALID()) {
    NOTREACHED();
    return NULL;
  }

  // Verify that this method is being called on the UI thread.
  if (!CefThread::CurrentlyOn(CefThread::UI)) {
    NOTREACHED();
    return NULL;
  }

  CefString newUrl = url;
  CefRefPtr<CefBrowser> alternateBrowser;
  CefBrowserSettings settings(_Context->browser_defaults());

  if(handler.get())
  {
    // Give the handler an opportunity to modify window attributes, handler,
    // or cancel the window creation.
    CefHandler::RetVal rv = handler->HandleBeforeCreated(NULL, windowInfo,
        popup, CefPopupFeatures(), handler, newUrl, settings);
    if(rv == RV_HANDLED)
      return false;
  }

  CefRefPtr<CefBrowser> browser(
      new CefBrowserImpl(windowInfo, settings, popup, handler));
  static_cast<CefBrowserImpl*>(browser.get())->UIT_CreateBrowser(newUrl);

  return browser;
}


CefBrowserImpl::CefBrowserImpl(const CefWindowInfo& windowInfo,
                               const CefBrowserSettings& settings, bool popup,
                               CefRefPtr<CefHandler> handler)
  : window_info_(windowInfo), settings_(settings), is_popup_(popup),
    is_modal_(false), handler_(handler), webviewhost_(NULL), popuphost_(NULL),
    zoom_level_(0.0), can_go_back_(false), can_go_forward_(false),
    main_frame_(NULL), unique_id_(0)
{
  delegate_.reset(new BrowserWebViewDelegate(this));
  popup_delegate_.reset(new BrowserWebViewDelegate(this));
  nav_controller_.reset(new BrowserNavigationController(this));

  if (!file_system_root_.CreateUniqueTempDir()) {
    LOG(WARNING) << "Failed to create a temp dir for the filesystem."
                    "FileSystem feature will be disabled.";
    DCHECK(file_system_root_.path().empty());
  }
}

void CefBrowserImpl::CloseBrowser()
{
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_CloseBrowser));
}
void CefBrowserImpl::GoBack()
{
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_HandleActionView, MENU_ID_NAV_BACK));
}

void CefBrowserImpl::GoForward()
{
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_HandleActionView, MENU_ID_NAV_FORWARD));
}

void CefBrowserImpl::Reload()
{
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_HandleActionView, MENU_ID_NAV_RELOAD));
}

void CefBrowserImpl::ReloadIgnoreCache()
{
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_HandleActionView, MENU_ID_NAV_RELOAD_NOCACHE));
}

void CefBrowserImpl::StopLoad()
{
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_HandleActionView, MENU_ID_NAV_STOP));
}

void CefBrowserImpl::SetFocus(bool enable)
{
  if (CefThread::CurrentlyOn(CefThread::UI)) {
    UIT_SetFocus(UIT_GetWebViewHost(), enable);
  } else {
    CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
        &CefBrowserImpl::UIT_SetFocus, UIT_GetWebViewHost(), enable));
  }
}

CefRefPtr<CefFrame> CefBrowserImpl::GetFocusedFrame()
{
  // Verify that this method is being called on the UI thread.
  if (!CefThread::CurrentlyOn(CefThread::UI)) {
    NOTREACHED();
    return NULL;
  }
  
  WebView* view = UIT_GetWebView();
  return view ? UIT_GetCefFrame(view->focusedFrame()) : NULL;
}

CefRefPtr<CefFrame> CefBrowserImpl::GetFrame(const CefString& name)
{
  // Verify that this method is being called on the UI thread.
  if (!CefThread::CurrentlyOn(CefThread::UI)) {
    NOTREACHED();
    return NULL;
  }
  
  WebView* view = UIT_GetWebView();
  if (!view)
    return NULL;

  WebFrame* frame = view->findFrameByName(string16(name));
  if(frame)
    return UIT_GetCefFrame(frame);
  return NULL;
}

void CefBrowserImpl::GetFrameNames(std::vector<CefString>& names)
{
  // Verify that this method is being called on the UI thread.
  if (!CefThread::CurrentlyOn(CefThread::UI)) {
    NOTREACHED();
    return;
  }
  
  WebView* view = UIT_GetWebView();
  if (!view)
    return;

  WebFrame* main_frame = view->mainFrame();
  WebFrame* it = main_frame;
  do {
    if(it != main_frame) {
      string16 str = it->name();
      names.push_back(str);
    }
    it = it->traverseNext(true);
  } while (it != main_frame);
}

void CefBrowserImpl::Find(int identifier, const CefString& searchText,
                          bool forward, bool matchCase, bool findNext)
{
  WebKit::WebFindOptions options;
  options.forward = forward;
  options.matchCase = matchCase;
  options.findNext = findNext;

  // Execute the request on the UI thread.
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_Find, identifier, searchText, options));
}

void CefBrowserImpl::StopFinding(bool clearSelection)
{
  // Execute the request on the UI thread.
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_StopFinding, clearSelection));
}

void CefBrowserImpl::SetZoomLevel(double zoomLevel)
{
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_SetZoomLevel, zoomLevel));
}

void CefBrowserImpl::ShowDevTools()
{
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_ShowDevTools));
}

void CefBrowserImpl::CloseDevTools()
{
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_CloseDevTools));
}

bool CefBrowserImpl::GetSize(PaintElementType type, int& width, int& height)
{
  if (!CefThread::CurrentlyOn(CefThread::UI)) {
    NOTREACHED();
    return false;
  }

  width = height = 0;

  if(type == PET_VIEW) {
    WebViewHost* host = UIT_GetWebViewHost();
    if (host) {
      host->GetSize(width, height);
      return true;
    }
  } else if(type == PET_POPUP) {
    if (popuphost_) {
      popuphost_->GetSize(width, height);
      return true;
    }
  }

  return false;
}

void CefBrowserImpl::SetSize(PaintElementType type, int width, int height)
{
  // Intentially post event tasks in all cases so that painting tasks can be
  // handled at sane times.
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_SetSize, type, width, height));
}

bool CefBrowserImpl::IsPopupVisible()
{
  if (!CefThread::CurrentlyOn(CefThread::UI)) {
    NOTREACHED();
    return false;
  }
  
  return (popuphost_ != NULL);
}

void CefBrowserImpl::HidePopup()
{
  // Intentially post event tasks in all cases so that painting tasks can be
  // handled at sane times.
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_ClosePopupWidget));
}

void CefBrowserImpl::Invalidate(const CefRect& dirtyRect)
{
  // Intentially post event tasks in all cases so that painting tasks can be
  // handled at sane times.
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_Invalidate, dirtyRect));
}

bool CefBrowserImpl::GetImage(PaintElementType type, int width, int height,
                              void* buffer)
{
  if (!CefThread::CurrentlyOn(CefThread::UI)) {
    NOTREACHED();
    return false;
  }

  if(type == PET_VIEW) {
    WebViewHost* host = UIT_GetWebViewHost();
    if (host)
      return host->GetImage(width, height, buffer);
  } else if(type == PET_POPUP) {
    if (popuphost_)
     return popuphost_->GetImage(width, height, buffer);
  }

  return false;
}

void CefBrowserImpl::SendKeyEvent(KeyType type, int key, int modifiers,
                                  bool sysChar, bool imeChar)
{
  // Intentially post event tasks in all cases so that painting tasks can be
  // handled at sane times.
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_SendKeyEvent, type, key, modifiers, sysChar,
      imeChar));
}

void CefBrowserImpl::SendMouseClickEvent(int x, int y, MouseButtonType type,
                                         bool mouseUp, int clickCount)
{
  // Intentially post event tasks in all cases so that painting tasks can be
  // handled at sane times.
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_SendMouseClickEvent, x, y, type, mouseUp,
      clickCount));
}

void CefBrowserImpl::SendMouseMoveEvent(int x, int y, bool mouseLeave)
{
  // Intentially post event tasks in all cases so that painting tasks can be
  // handled at sane times.
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_SendMouseMoveEvent, x, y, mouseLeave));
}

void CefBrowserImpl::SendMouseWheelEvent(int x, int y, int delta)
{
  // Intentially post event tasks in all cases so that painting tasks can be
  // handled at sane times.
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_SendMouseWheelEvent, x, y, delta));
}

void CefBrowserImpl::SendFocusEvent(bool setFocus)
{
  // Intentially post event tasks in all cases so that painting tasks can be
  // handled at sane times.
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_SendFocusEvent, setFocus));
}

void CefBrowserImpl::SendCaptureLostEvent()
{
  // Intentially post event tasks in all cases so that painting tasks can be
  // handled at sane times.
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_SendCaptureLostEvent));
}

void CefBrowserImpl::Undo(CefRefPtr<CefFrame> frame)
{
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_HandleAction, MENU_ID_UNDO, frame));
}

void CefBrowserImpl::Redo(CefRefPtr<CefFrame> frame)
{
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_HandleAction, MENU_ID_REDO, frame));
}

void CefBrowserImpl::Cut(CefRefPtr<CefFrame> frame)
{
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_HandleAction, MENU_ID_CUT, frame));
}

void CefBrowserImpl::Copy(CefRefPtr<CefFrame> frame)
{
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_HandleAction, MENU_ID_COPY, frame));
}

void CefBrowserImpl::Paste(CefRefPtr<CefFrame> frame)
{
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_HandleAction, MENU_ID_PASTE, frame));
}

void CefBrowserImpl::Delete(CefRefPtr<CefFrame> frame)
{
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_HandleAction, MENU_ID_DELETE, frame));
}

void CefBrowserImpl::SelectAll(CefRefPtr<CefFrame> frame)
{
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_HandleAction, MENU_ID_SELECTALL, frame));
}


void CefBrowserImpl::Print(CefRefPtr<CefFrame> frame)
{
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_HandleAction, MENU_ID_PRINT, frame));
}

void CefBrowserImpl::ViewSource(CefRefPtr<CefFrame> frame)
{
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_HandleAction, MENU_ID_VIEWSOURCE, frame));
}

CefString CefBrowserImpl::GetSource(CefRefPtr<CefFrame> frame)
{
  // Verify that this method is being called on the UI thread.
  if (!CefThread::CurrentlyOn(CefThread::UI)) {
    NOTREACHED();
    return CefString();
  }
  
  // Retrieve the document string directly
  WebKit::WebFrame* web_frame = UIT_GetWebFrame(frame);
  if(web_frame)
    return string16(web_frame->contentAsMarkup());
  return CefString();
}

CefString CefBrowserImpl::GetText(CefRefPtr<CefFrame> frame)
{
  // Verify that this method is being called on the UI thread.
  if (!CefThread::CurrentlyOn(CefThread::UI)) {
    NOTREACHED();
    return CefString();
  }
  
  // Retrieve the document text directly
  WebKit::WebFrame* web_frame = UIT_GetWebFrame(frame);
  if(web_frame)
    return webkit_glue::DumpDocumentText(web_frame);
  return CefString();
}

void CefBrowserImpl::LoadRequest(CefRefPtr<CefFrame> frame,
                                 CefRefPtr<CefRequest> request)
{
  DCHECK(request.get() != NULL);
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_LoadURLForRequestRef, frame, request));
}

void CefBrowserImpl::LoadURL(CefRefPtr<CefFrame> frame,
                             const CefString& url)
{
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_LoadURL, frame, url));
}

void CefBrowserImpl::LoadString(CefRefPtr<CefFrame> frame,
                                const CefString& string,
                                const CefString& url)
{
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_LoadHTML, frame, string, url));
}

void CefBrowserImpl::LoadStream(CefRefPtr<CefFrame> frame,
                                CefRefPtr<CefStreamReader> stream,
                                const CefString& url)
{
  DCHECK(stream.get() != NULL);
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_LoadHTMLForStreamRef, frame, stream, url));
}

void CefBrowserImpl::ExecuteJavaScript(CefRefPtr<CefFrame> frame,
                                       const CefString& jsCode, 
                                       const CefString& scriptUrl,
                                       int startLine)
{
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(this,
      &CefBrowserImpl::UIT_ExecuteJavaScript, frame, jsCode, scriptUrl,
      startLine));
}

CefString CefBrowserImpl::GetURL(CefRefPtr<CefFrame> frame)
{
  // Verify that this method is being called on the UI thread.
  if (!CefThread::CurrentlyOn(CefThread::UI)) {
    NOTREACHED();
    return CefString();
  }

  WebFrame* web_frame = UIT_GetWebFrame(frame);
  if(web_frame)
    return std::string(web_frame->url().spec());
  return CefString();
}

CefRefPtr<CefFrame> CefBrowserImpl::GetCefFrame(const CefString& name)
{
  CefRefPtr<CefFrame> cef_frame;

  if(name.empty()) {
    // Use the single main frame reference.
    cef_frame = GetMainCefFrame();
  } else {
    // Locate or create the appropriate named reference.
    AutoLock lock_scope(this);
    FrameMap::const_iterator it = frames_.find(name);
    if(it != frames_.end())
      cef_frame = it->second;
    else {
      cef_frame = new CefFrameImpl(this, name);
      frames_.insert(std::make_pair(name, cef_frame.get()));
    }
  }

  return cef_frame;
}

void CefBrowserImpl::RemoveCefFrame(const CefString& name)
{
  AutoLock lock_scope(this);
  if(name.empty()) {
    // Clear the single main frame reference.
    main_frame_ = NULL;
  } else {
    // Remove the appropriate named reference.
    FrameMap::iterator it = frames_.find(name);
    if(it != frames_.end())
      frames_.erase(it);
  }
}

CefRefPtr<CefFrame> CefBrowserImpl::GetMainCefFrame()
{
  // Return the single main frame reference.
  AutoLock lock_scope(this);
  if(main_frame_ == NULL)
    main_frame_ = new CefFrameImpl(this, CefString());
  return main_frame_;
}

CefRefPtr<CefFrame> CefBrowserImpl::UIT_GetCefFrame(WebFrame* frame)
{
  REQUIRE_UIT();

  CefRefPtr<CefFrame> cef_frame;

  if(frame->parent() == 0) {
    // Use the single main frame reference.
    cef_frame = GetMainCefFrame();
  } else {
    // Locate or create the appropriate named reference.
    CefString name = string16(frame->name());
    DCHECK(!name.empty());
    cef_frame = GetCefFrame(name);
  }

  return cef_frame;
}

WebFrame* CefBrowserImpl::UIT_GetMainWebFrame()
{
  REQUIRE_UIT();

  WebView* view = UIT_GetWebView();
  if (view)
    return view ->mainFrame();
  return NULL;
}

WebFrame* CefBrowserImpl::UIT_GetWebFrame(CefRefPtr<CefFrame> frame)
{
  REQUIRE_UIT();

  WebView* view = UIT_GetWebView();
  if (!view)
    return NULL;

  CefString name = frame->GetName();
  if(name.empty())
    return view ->mainFrame();
  return view ->findFrameByName(string16(name));
}

void CefBrowserImpl::UIT_DestroyBrowser()
{
  if(handler_.get()) {
    // Notify the handler that the window is about to be closed.
    handler_->HandleBeforeWindowClose(this);
  }
  UIT_GetWebViewDelegate()->RevokeDragDrop();

  // If the current browser window is a dev tools client then disconnect from
  // the agent and destroy the client before destroying the window.
  UIT_DestroyDevToolsClient();
  
  if (dev_tools_agent_.get()) {
    BrowserDevToolsClient* client = dev_tools_agent_->client();
    if (client) {
      CefBrowserImpl* browser = client->browser();
      // Destroy the client before freeing the agent.
      browser->UIT_DestroyDevToolsClient();
      if(!_Context->shutting_down()) {
        // Explicitly close the client browser window.
        browser->UIT_CloseBrowser();
      }
    }
    // Free the agent.
    dev_tools_agent_.reset();
  }

  // Clean up anything associated with the WebViewHost widget.
  UIT_GetWebViewHost()->webwidget()->close();
  webviewhost_.reset();
  
  // Remove the reference added in UIT_CreateBrowser().
  Release();
  
  // Remove the browser from the list maintained by the context.
  _Context->RemoveBrowser(this);
}

void CefBrowserImpl::UIT_CloseBrowser()
{
  REQUIRE_UIT();
  if (!IsWindowRenderingDisabled())
    UIT_CloseView(UIT_GetMainWndHandle());
  else
    UIT_DestroyBrowser();
}

void CefBrowserImpl::UIT_LoadURL(CefRefPtr<CefFrame> frame,
                                 const CefString& url)
{
  UIT_LoadURLForRequest(frame, url, CefString(), WebHTTPBody(),
      CefRequest::HeaderMap());
}

void CefBrowserImpl::UIT_LoadURLForRequestRef(CefRefPtr<CefFrame> frame,
                                              CefRefPtr<CefRequest> request)
{
  CefString url = request->GetURL();
  CefString method = request->GetMethod();

  CefRequestImpl *impl = static_cast<CefRequestImpl*>(request.get());

  WebHTTPBody upload_data;
  CefRefPtr<CefPostData> postdata = impl->GetPostData();
  if(postdata.get()) {
    upload_data.initialize();
    static_cast<CefPostDataImpl*>(postdata.get())->Get(upload_data);
  }

  CefRequest::HeaderMap headers;
  impl->GetHeaderMap(headers);

  UIT_LoadURLForRequest(frame, url, method, upload_data, headers);
}

void CefBrowserImpl::UIT_LoadURLForRequest(CefRefPtr<CefFrame> frame,
                                         const CefString& url,
                                         const CefString& method,
                                         const WebKit::WebHTTPBody& upload_data,
                                         const CefRequest::HeaderMap& headers)
{
  REQUIRE_UIT();
  
  if (url.empty())
      return;

  std::string urlStr(url);
  GURL gurl = GURL(urlStr);

  if (!gurl.is_valid() && !gurl.has_scheme()) {
    // Try to add "http://" at the beginning
    std::string new_url = std::string("http://") + urlStr;
    gurl = GURL(new_url);
    if (!gurl.is_valid())
      return;
  }

  nav_controller_->LoadEntry(
      new BrowserNavigationEntry(-1, gurl, CefString(), frame->GetName(),
          method, upload_data, headers));
}

void CefBrowserImpl::UIT_LoadHTML(CefRefPtr<CefFrame> frame,
                                  const CefString& html,
                                  const CefString& url)
{
  REQUIRE_UIT();

  std::string urlStr(url);
  GURL gurl = GURL(urlStr);

  if (!gurl.is_valid() && !gurl.has_scheme()) {
    // Try to add "http://" at the beginning
    std::string new_url = std::string("http://") + urlStr;
    gurl = GURL(new_url);
    if (!gurl.is_valid())
      return;
  }

  WebFrame* web_frame = UIT_GetWebFrame(frame);
  if(web_frame)
    web_frame->loadHTMLString(std::string(html), gurl);
}

void CefBrowserImpl::UIT_LoadHTMLForStreamRef(CefRefPtr<CefFrame> frame,
                                              CefRefPtr<CefStreamReader> stream,
                                              const CefString& url)
{
  REQUIRE_UIT();
  
  std::string urlStr(url);
  GURL gurl = GURL(urlStr);

  if (!gurl.is_valid() && !gurl.has_scheme()) {
    // Try to add "http://" at the beginning
    std::string new_url = std::string("http://") + urlStr;
    gurl = GURL(new_url);
    if (!gurl.is_valid())
      return;
  }

  // read all of the stream data into a std::string.
  std::stringstream ss;
  char buff[BUFFER_SIZE];
  size_t read;
  do {
    read = stream->Read(buff, sizeof(char), BUFFER_SIZE-1);
    if(read > 0) {
      buff[read] = 0;
      ss << buff;
    }
  }
  while(read > 0);

  WebFrame* web_frame = UIT_GetWebFrame(frame);
  if(web_frame)
    web_frame->loadHTMLString(ss.str(), gurl);
}

void CefBrowserImpl::UIT_ExecuteJavaScript(CefRefPtr<CefFrame> frame,
                                           const CefString& js_code, 
                                           const CefString& script_url,
                                           int start_line)
{
  REQUIRE_UIT();

  WebFrame* web_frame = UIT_GetWebFrame(frame);
  if(web_frame) {
    web_frame->executeScript(WebScriptSource(string16(js_code),
        WebURL(GURL(std::string(script_url))), start_line));
  }
}

void CefBrowserImpl::UIT_GoBackOrForward(int offset)
{
  REQUIRE_UIT();
  nav_controller_->GoToOffset(offset);
}

void CefBrowserImpl::UIT_Reload(bool ignoreCache)
{
  REQUIRE_UIT();
  nav_controller_->Reload(ignoreCache);
}

bool CefBrowserImpl::UIT_Navigate(const BrowserNavigationEntry& entry,
                                  bool reload,
                                  bool ignoreCache)
{
  REQUIRE_UIT();

  WebView* view = UIT_GetWebView();
  if (!view)
    return false;

  // Get the right target frame for the entry.
  WebFrame* frame;
  if (!entry.GetTargetFrame().empty())
    frame = view->findFrameByName(string16(entry.GetTargetFrame()));
  else
    frame = view->mainFrame();

  // TODO(mpcomplete): should we clear the target frame, or should
  // back/forward navigations maintain the target frame?

  // A navigation resulting from loading a javascript URL should not be
  // treated as a browser initiated event.  Instead, we want it to look as if
  // the page initiated any load resulting from JS execution.
  if (!entry.GetURL().SchemeIs("javascript")) {
    delegate_->set_pending_extra_data(
        new BrowserExtraData(entry.GetPageID()));
  }

  // If we are reloading, then WebKit will use the state of the current page.
  // Otherwise, we give it the state to navigate to.
  if (reload) {
    frame->reload(ignoreCache);
  } else if (!entry.GetContentState().empty()) {
    DCHECK(entry.GetPageID() != -1);
    frame->loadHistoryItem(
        webkit_glue::HistoryItemFromString(entry.GetContentState()));
  } else {
    DCHECK(entry.GetPageID() == -1);
    WebURLRequest request(entry.GetURL());

    if(entry.GetMethod().length() > 0)
      request.setHTTPMethod(string16(entry.GetMethod()));

    if(entry.GetHeaders().size() > 0)
      CefRequestImpl::SetHeaderMap(entry.GetHeaders(), request);

    if(!entry.GetUploadData().isNull())
    {
      string16 method = request.httpMethod();
      if(method == ASCIIToUTF16("GET") || method == ASCIIToUTF16("HEAD"))
        request.setHTTPMethod(ASCIIToUTF16("POST"));

      if(request.httpHeaderField(ASCIIToUTF16("Content-Type")).length() == 0) {
        request.setHTTPHeaderField(
            ASCIIToUTF16("Content-Type"),
            ASCIIToUTF16("application/x-www-form-urlencoded"));
      }
      request.setHTTPBody(entry.GetUploadData());
    }

    frame->loadRequest(request);
  }

  // In case LoadRequest failed before DidCreateDataSource was called.
  delegate_->set_pending_extra_data(NULL);

  if (handler_.get() && handler_->HandleSetFocus(this, false) == RV_CONTINUE) {
    // Restore focus to the main frame prior to loading new request.
    // This makes sure that we don't have a focused iframe. Otherwise, that
    // iframe would keep focus when the SetFocus called immediately after
    // LoadRequest, thus making some tests fail (see http://b/issue?id=845337
    // for more details).
    // TODO(cef): The above comment may be wrong, or the below call to
    // setFocusedFrame() may be unnecessary or in the wrong place.  See this
    // thread for additional details:
    // http://groups.google.com/group/chromium-dev/browse_thread/thread/42bcd31b59e3a168
    view->setFocusedFrame(frame);

    // Give focus to the window if it is currently visible.
    if (!IsWindowRenderingDisabled() &&
        UIT_IsViewVisible(UIT_GetMainWndHandle()))
      UIT_SetFocus(UIT_GetWebViewHost(), true);
  }

  return true;
}


void CefBrowserImpl::UIT_SetSize(PaintElementType type, int width, int height)
{
  if(type == PET_VIEW) {
    WebViewHost* host = UIT_GetWebViewHost();
    if (host)
      host->SetSize(width, height);
  } else if(type == PET_POPUP) {
    if (popuphost_)
      popuphost_->SetSize(width, height);
  }
}

void CefBrowserImpl::UIT_Invalidate(const CefRect& dirtyRect)
{
  REQUIRE_UIT();
  WebViewHost* host = UIT_GetWebViewHost();
  if (host) {
    host->InvalidateRect(gfx::Rect(dirtyRect.x, dirtyRect.y, dirtyRect.width,
                                   dirtyRect.height));
  }
}

void CefBrowserImpl::UIT_SendKeyEvent(KeyType type, int key, int modifiers,
                                      bool sysChar, bool imeChar)
{
  REQUIRE_UIT();
  if (popuphost_) {
    // Send the event to the popup.
    popuphost_->SendKeyEvent(type, key, modifiers, sysChar, imeChar);
  } else {
    WebViewHost* host = UIT_GetWebViewHost();
    if (host)
      host->SendKeyEvent(type, key, modifiers, sysChar, imeChar);
  }
}

void CefBrowserImpl::UIT_SendMouseClickEvent(int x, int y, MouseButtonType type,
                                             bool mouseUp, int clickCount)
{
  REQUIRE_UIT();
  if (popuphost_ && popup_rect_.Contains(x, y)) {
    // Send the event to the popup.
    popuphost_->SendMouseClickEvent(x - popup_rect_.x(), y - popup_rect_.y(),
        type, mouseUp, clickCount);
  } else {
    WebViewHost* host = UIT_GetWebViewHost();
    if (host)
      host->SendMouseClickEvent(x, y, type, mouseUp, clickCount);
  }
}

void CefBrowserImpl::UIT_SendMouseMoveEvent(int x, int y, bool mouseLeave)
{
  REQUIRE_UIT();
  if (popuphost_ && popup_rect_.Contains(x, y)) {
    // Send the event to the popup.
    popuphost_->SendMouseMoveEvent(x - popup_rect_.x(), y - popup_rect_.y(),
        mouseLeave);
  } else {
    WebViewHost* host = UIT_GetWebViewHost();
    if (host)
      host->SendMouseMoveEvent(x, y, mouseLeave);
  }
}

void CefBrowserImpl::UIT_SendMouseWheelEvent(int x, int y, int delta)
{
  REQUIRE_UIT();
  if (popuphost_ && popup_rect_.Contains(x, y)) {
    // Send the event to the popup.
    popuphost_->SendMouseWheelEvent(x - popup_rect_.x(), y - popup_rect_.y(),
        delta);
  } else {
    WebViewHost* host = UIT_GetWebViewHost();
    if (host)
      host->SendMouseWheelEvent(x, y, delta);
  }
}

void CefBrowserImpl::UIT_SendFocusEvent(bool setFocus)
{
  REQUIRE_UIT();
  WebViewHost* host = UIT_GetWebViewHost();
  if (host)
    host->SendFocusEvent(setFocus);
}

void CefBrowserImpl::UIT_SendCaptureLostEvent()
{
  REQUIRE_UIT();
  WebViewHost* host = UIT_GetWebViewHost();
  if (host)
    host->SendCaptureLostEvent();
}

CefRefPtr<CefBrowserImpl> CefBrowserImpl::UIT_CreatePopupWindow(
    const CefString& url, const CefPopupFeatures& features)
{
  REQUIRE_UIT();
    
  CefWindowInfo info;
#if defined(OS_WIN)
  info.SetAsPopup(NULL, CefString());
#endif

  // Default to the size from the popup features.
  if(features.xSet)
    info.m_x = features.x;
  if(features.ySet)
    info.m_y = features.y;
  if(features.widthSet)
    info.m_nWidth = features.width;
  if(features.heightSet)
    info.m_nHeight = features.height;

  CefRefPtr<CefHandler> handler = handler_;
  CefString newUrl = url;

  // Start with the current browser window's settings.
  CefBrowserSettings settings(settings_);

  if(handler_.get())
  {
    // Give the handler an opportunity to modify window attributes, handler,
    // or cancel the window creation.
    CefHandler::RetVal rv = handler_->HandleBeforeCreated(this, info, true,
        features, handler, newUrl, settings);
    if(rv == RV_HANDLED)
      return NULL;
  }

  CefRefPtr<CefBrowserImpl> browser(
      new CefBrowserImpl(info, settings, true, handler));
  browser->UIT_CreateBrowser(newUrl);

  return browser;
}

WebKit::WebWidget* CefBrowserImpl::UIT_CreatePopupWidget()
{
  REQUIRE_UIT();

  DCHECK(!popuphost_);
  popuphost_ = WebWidgetHost::Create(
      (IsWindowRenderingDisabled()?NULL:UIT_GetMainWndHandle()),
      popup_delegate_.get(), paint_delegate_.get());
  popuphost_->set_popup(true);

  return popuphost_->webwidget();
}

void CefBrowserImpl::UIT_ClosePopupWidget()
{
  REQUIRE_UIT();

  if (!popuphost_)
    return;
  
#if !defined(OS_MACOSX)
  // Mac uses a WebPopupMenu for select lists so no closing is necessary.
  if (!IsWindowRenderingDisabled())
    UIT_CloseView(UIT_GetPopupWndHandle());
#endif
  popuphost_ = NULL;
  popup_rect_ = gfx::Rect();

  if (IsWindowRenderingDisabled() && handler_.get()) {
    // Notify the handler of popup visibility change.
    handler_->HandlePopupChange(this, false, CefRect());
  }
}

void CefBrowserImpl::UIT_Show(WebKit::WebNavigationPolicy policy)
{
  REQUIRE_UIT();
  delegate_->show(policy);
}

void CefBrowserImpl::UIT_HandleActionView(CefHandler::MenuId menuId)
{
  return UIT_HandleAction(menuId, NULL);
}

void CefBrowserImpl::UIT_HandleAction(CefHandler::MenuId menuId,
                                      CefRefPtr<CefFrame> frame)
{
  REQUIRE_UIT();

  WebFrame* web_frame = NULL;
  if(frame)
    web_frame = UIT_GetWebFrame(frame);

  switch(menuId)
  {
    case MENU_ID_NAV_BACK:
      UIT_GoBackOrForward(-1);
      break;
    case MENU_ID_NAV_FORWARD:
      UIT_GoBackOrForward(1);
      break;
    case MENU_ID_NAV_RELOAD:
      UIT_Reload(false);
      break;
    case MENU_ID_NAV_RELOAD_NOCACHE:
      UIT_Reload(true);
      break;
    case MENU_ID_NAV_STOP:
      if (UIT_GetWebView()) 
        UIT_GetWebView()->mainFrame()->stopLoading();
      break;
    case MENU_ID_UNDO:
      if(web_frame)
        web_frame->executeCommand(WebString::fromUTF8("Undo"));
      break;
    case MENU_ID_REDO:
      if(web_frame)
        web_frame->executeCommand(WebString::fromUTF8("Redo"));
      break;
    case MENU_ID_CUT:
      if(web_frame)
        web_frame->executeCommand(WebString::fromUTF8("Cut"));
      break;
    case MENU_ID_COPY:
      if(web_frame)
        web_frame->executeCommand(WebString::fromUTF8("Copy"));
      break;
    case MENU_ID_PASTE:
      if(web_frame)
        web_frame->executeCommand(WebString::fromUTF8("Paste"));
      break;
    case MENU_ID_DELETE:
      if(web_frame)
        web_frame->executeCommand(WebString::fromUTF8("Delete"));
      break;
    case MENU_ID_SELECTALL:
      if(web_frame)
        web_frame->executeCommand(WebString::fromUTF8("SelectAll"));
      break;
    case MENU_ID_PRINT:
      if(web_frame)
        UIT_PrintPages(web_frame);
      break;
    case MENU_ID_VIEWSOURCE:
      if(web_frame)
        UIT_ViewDocumentString(web_frame);
      break;
  }
}

void CefBrowserImpl::UIT_Find(int identifier, const CefString& search_text,
                              const WebKit::WebFindOptions& options)
{
  WebView* view = UIT_GetWebView();
  if (!view)
    return;

  WebFrame* main_frame = view->mainFrame();
  string16 searchText(search_text);

  if (main_frame->document().isPluginDocument()) {
    WebPlugin* plugin = main_frame->document().to<WebPluginDocument>().plugin();
    webkit::npapi::WebPluginDelegate* delegate =
        static_cast<webkit::npapi::WebPluginImpl*>(plugin)->delegate();
    if (options.findNext) {
      // Just navigate back/forward.
      delegate->SelectFindResult(options.forward);
    } else {
      if (delegate->StartFind(searchText, options.matchCase, identifier)) {
      } else {
        // No find results.
        UIT_NotifyFindStatus(identifier, 0, gfx::Rect(), 0, true);
      }
    }
    return;
  }

  WebFrame* frame_after_main = main_frame->traverseNext(true);
  WebFrame* focused_frame = view->focusedFrame();
  WebFrame* search_frame = focused_frame;  // start searching focused frame.

  bool multi_frame = (frame_after_main != main_frame);

  // If we have multiple frames, we don't want to wrap the search within the
  // frame, so we check here if we only have main_frame in the chain.
  bool wrap_within_frame = !multi_frame;

  WebRect selection_rect;
  bool result = false;

  // If something is selected when we start searching it means we cannot just
  // increment the current match ordinal; we need to re-generate it.
  WebRange current_selection = focused_frame->selectionRange();

  do {
    result = search_frame->find(identifier, searchText, options,
        wrap_within_frame, &selection_rect);

    if (!result) {
      // don't leave text selected as you move to the next frame.
      search_frame->executeCommand(WebString::fromUTF8("Unselect"));

      // Find the next frame, but skip the invisible ones.
      do {
        // What is the next frame to search? (we might be going backwards). Note
        // that we specify wrap=true so that search_frame never becomes NULL.
        search_frame = options.forward ?
            search_frame->traverseNext(true) :
            search_frame->traversePrevious(true);
      } while (!search_frame->hasVisibleContent() &&
               search_frame != focused_frame);

      // Make sure selection doesn't affect the search operation in new frame.
      search_frame->executeCommand(WebString::fromUTF8("Unselect"));

      // If we have multiple frames and we have wrapped back around to the
      // focused frame, we need to search it once more allowing wrap within
      // the frame, otherwise it will report 'no match' if the focused frame has
      // reported matches, but no frames after the focused_frame contain a
      // match for the search word(s).
      if (multi_frame && search_frame == focused_frame) {
        result = search_frame->find(
            identifier, searchText,
            options, true,  // Force wrapping.
            &selection_rect);
      }
    }

    view->setFocusedFrame(search_frame);
  } while (!result && search_frame != focused_frame);

  if (options.findNext && current_selection.isNull()) {
    // Force the main_frame to report the actual count.
    main_frame->increaseMatchCount(0, identifier);
  } else {
    // If nothing is found, set result to "0 of 0", otherwise, set it to
    // "-1 of 1" to indicate that we found at least one item, but we don't know
    // yet what is active.
    int ordinal = result ? -1 : 0;  // -1 here means, we might know more later.
    int match_count = result ? 1 : 0;  // 1 here means possibly more coming.

    // If we find no matches then this will be our last status update.
    // Otherwise the scoping effort will send more results.
    bool final_status_update = !result;

    // Send the search result.
    UIT_NotifyFindStatus(identifier, match_count, selection_rect, ordinal,
        final_status_update);

    // Scoping effort begins, starting with the mainframe.
    search_frame = main_frame;

    main_frame->resetMatchCount();

    do {
      // Cancel all old scoping requests before starting a new one.
      search_frame->cancelPendingScopingEffort();

      // We don't start another scoping effort unless at least one match has
      // been found.
      if (result) {
        // Start new scoping request. If the scoping function determines that it
        // needs to scope, it will defer until later.
        search_frame->scopeStringMatches(identifier, searchText, options,
                                         true);  // reset the tickmarks
      }

      // Iterate to the next frame. The frame will not necessarily scope, for
      // example if it is not visible.
      search_frame = search_frame->traverseNext(true);
    } while (search_frame != main_frame);
  }
}

void CefBrowserImpl::UIT_StopFinding(bool clear_selection)
{
  WebView* view = UIT_GetWebView();
  if (!view)
    return;

  WebDocument doc = view->mainFrame()->document();
  if (doc.isPluginDocument()) {
    WebPlugin* plugin = view->mainFrame()->document().
        to<WebPluginDocument>().plugin();
    webkit::npapi::WebPluginDelegate* delegate =
        static_cast<webkit::npapi::WebPluginImpl*>(plugin)->delegate();
    delegate->StopFind();
    return;
  }

  if (clear_selection)
    view->focusedFrame()->executeCommand(WebString::fromUTF8("Unselect"));

  WebFrame* frame = view->mainFrame();
  while (frame) {
    frame->stopFinding(clear_selection);
    frame = frame->traverseNext(false);
  }
}

void CefBrowserImpl::UIT_NotifyFindStatus(int identifier, int count,
                                          const WebKit::WebRect& selection_rect,
                                          int active_match_ordinal,
                                          bool final_update)
{
  if(handler_.get()) {
    CefRect rect(selection_rect.x, selection_rect.y, selection_rect.width,
        selection_rect.height);
    handler_->HandleFindResult(this, identifier, count, rect,
        active_match_ordinal, final_update);
  }
}

void CefBrowserImpl::UIT_SetZoomLevel(double zoomLevel)
{
  REQUIRE_UIT();
  WebKit::WebFrame* web_frame = UIT_GetMainWebFrame();
  if(web_frame) {
    web_frame->view()->setZoomLevel(false, zoomLevel);
    ZoomMap::GetInstance()->set(web_frame->url(), zoomLevel);
    set_zoom_level(zoomLevel);
  }
}

void CefBrowserImpl::UIT_ShowDevTools()
{
  REQUIRE_UIT();

  if(!dev_tools_agent_.get())
    return;

  BrowserDevToolsClient* client = dev_tools_agent_->client();
  if (!client) {
    // Create the inspector window.
    FilePath dir_exe;
    PathService::Get(base::DIR_EXE, &dir_exe);
    FilePath devtools_path =
        dir_exe.AppendASCII("resources/inspector/devtools.html");

    CefPopupFeatures features;
    CefRefPtr<CefBrowserImpl> browser =
        UIT_CreatePopupWindow(devtools_path.value(), features);
    browser->UIT_CreateDevToolsClient(dev_tools_agent_.get());
    browser->UIT_Show(WebKit::WebNavigationPolicyNewWindow);
  } else {
    // Give focus to the existing inspector window.
    client->browser()->UIT_Show(WebKit::WebNavigationPolicyNewWindow);
  }
}

void CefBrowserImpl::UIT_CloseDevTools()
{
  REQUIRE_UIT();

  if(!dev_tools_agent_.get())
    return;

  BrowserDevToolsClient* client = dev_tools_agent_->client();
  if (client)
    client->browser()->UIT_CloseBrowser();
}

void CefBrowserImpl::UIT_VisitDOM(CefRefPtr<CefFrame> frame,
                                  CefRefPtr<CefDOMVisitor> visitor)
{
  REQUIRE_UIT();

  WebKit::WebFrame* web_frame = UIT_GetWebFrame(frame);
  if (!web_frame)
    return;

  // Create a CefDOMDocumentImpl object that is valid only for the scope of this
  // method.
  CefRefPtr<CefDOMDocumentImpl> documentImpl;
  const WebKit::WebDocument& document = web_frame->document();
  if (!document.isNull())
    documentImpl = new CefDOMDocumentImpl(this, web_frame);

  visitor->Visit(documentImpl.get());

  if (documentImpl.get())
    documentImpl->Detach();
}

void CefBrowserImpl::UIT_AddFrameObject(WebKit::WebFrame* frame,
                                        CefTrackObject* tracked_object)
{
  REQUIRE_UIT();

  CefRefPtr<CefTrackManager> manager;

  if (!frame_objects_.empty()) {
    FrameObjectMap::const_iterator it = frame_objects_.find(frame);
    if (it != frame_objects_.end())
      manager = it->second;
  }

  if (!manager.get()) {
    manager = new CefTrackManager();
    frame_objects_.insert(std::make_pair(frame, manager));
  }

  manager->Add(tracked_object);
}

void CefBrowserImpl::UIT_BeforeFrameClosed(WebKit::WebFrame* frame)
{
  REQUIRE_UIT();

  if (!frame_objects_.empty()) {
    // Remove any tracked objects associated with the frame.
    FrameObjectMap::iterator it = frame_objects_.find(frame);
    if (it != frame_objects_.end())
      frame_objects_.erase(it);
  }
}

void CefBrowserImpl::set_zoom_level(double zoomLevel)
{
  AutoLock lock_scope(this);
  zoom_level_ = zoomLevel;
}

double CefBrowserImpl::zoom_level()
{
  AutoLock lock_scope(this);
  return zoom_level_;
}

void CefBrowserImpl::set_nav_state(bool can_go_back, bool can_go_forward)
{
  AutoLock lock_scope(this);
  can_go_back_ = can_go_back;
  can_go_forward_ = can_go_forward;
}

bool CefBrowserImpl::can_go_back()
{
  AutoLock lock_scope(this);
  return can_go_back_;
}

bool CefBrowserImpl::can_go_forward()
{
  AutoLock lock_scope(this);
  return can_go_forward_;
}

void CefBrowserImpl::UIT_CreateDevToolsClient(BrowserDevToolsAgent *agent)
{
  dev_tools_client_.reset(new BrowserDevToolsClient(this, agent));
}

void CefBrowserImpl::UIT_DestroyDevToolsClient()
{
  if (dev_tools_client_.get()) {
    // Free the client. This will cause the client to clear pending messages
    // and detach from the agent.
    dev_tools_client_.reset();
  }
}


// CefFrameImpl

CefFrameImpl::CefFrameImpl(CefBrowserImpl* browser, const CefString& name)
    : browser_(browser), name_(name)
{
}

CefFrameImpl::~CefFrameImpl()
{
  browser_->RemoveCefFrame(name_);
}

bool CefFrameImpl::IsFocused()
{
  // Verify that this method is being called on the UI thread.
  if (!CefThread::CurrentlyOn(CefThread::UI)) {
    NOTREACHED();
    return false;
  }

  return (browser_->UIT_GetWebView() &&
         (browser_->UIT_GetWebFrame(this) ==
            browser_->UIT_GetWebView()->focusedFrame()));
}

void CefFrameImpl::VisitDOM(CefRefPtr<CefDOMVisitor> visitor)
{
  if(!visitor.get()) {
    NOTREACHED();
    return;
  }
  CefRefPtr<CefFrame> framePtr(this);
  CefThread::PostTask(CefThread::UI, FROM_HERE, NewRunnableMethod(
      browser_.get(), &CefBrowserImpl::UIT_VisitDOM, framePtr, visitor));
}
