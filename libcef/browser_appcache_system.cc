// Copyright (c) 2009 The Chromium Authors. All rights reserved.  Use of this
// source code is governed by a BSD-style license that can be found in the
// LICENSE file.

#include "precompiled_libcef.h"
#include "browser_appcache_system.h"

#include "base/lock.h"
#include "base/task.h"
#include "base/waitable_event.h"
#include "webkit/appcache/appcache_interceptor.h"
#include "webkit/appcache/web_application_cache_host_impl.h"
#include "browser_resource_loader_bridge.h"

using WebKit::WebApplicationCacheHost;
using WebKit::WebApplicationCacheHostClient;
using appcache::WebApplicationCacheHostImpl;
using appcache::AppCacheBackendImpl;
using appcache::AppCacheInterceptor;


// BrowserFrontendProxy --------------------------------------------------------
// Proxies method calls from the backend IO thread to the frontend UI thread.

class BrowserFrontendProxy
    : public base::RefCountedThreadSafe<BrowserFrontendProxy>,
      public appcache::AppCacheFrontend {
 public:
  explicit BrowserFrontendProxy(BrowserAppCacheSystem* appcache_system)
      : system_(appcache_system) {
  }

  void clear_appcache_system() { system_ = NULL; }

  virtual void OnCacheSelected(int host_id, int64 cache_id ,
                               appcache::Status status) {
    if (!system_)
      return;
    if (system_->is_io_thread())
      system_->ui_message_loop()->PostTask(FROM_HERE, NewRunnableMethod(
          this, &BrowserFrontendProxy::OnCacheSelected,
          host_id, cache_id, status));
    else if (system_->is_ui_thread())
      system_->frontend_impl_.OnCacheSelected(host_id, cache_id, status);
    else
      NOTREACHED();
  }

  virtual void OnStatusChanged(const std::vector<int>& host_ids,
                               appcache::Status status) {
    if (!system_)
      return;
    if (system_->is_io_thread())
      system_->ui_message_loop()->PostTask(FROM_HERE, NewRunnableMethod(
          this, &BrowserFrontendProxy::OnStatusChanged, host_ids, status));
    else if (system_->is_ui_thread())
      system_->frontend_impl_.OnStatusChanged(host_ids, status);
    else
      NOTREACHED();
  }

  virtual void OnEventRaised(const std::vector<int>& host_ids,
                             appcache::EventID event_id) {
    if (!system_)
      return;
    if (system_->is_io_thread())
      system_->ui_message_loop()->PostTask(FROM_HERE, NewRunnableMethod(
          this, &BrowserFrontendProxy::OnEventRaised, host_ids, event_id));
    else if (system_->is_ui_thread())
      system_->frontend_impl_.OnEventRaised(host_ids, event_id);
    else
      NOTREACHED();
  }

 private:
  BrowserAppCacheSystem* system_;
};


// BrowserBackendProxy --------------------------------------------------------
// Proxies method calls from the frontend UI thread to the backend IO thread.

class BrowserBackendProxy
    : public base::RefCountedThreadSafe<BrowserBackendProxy>,
      public appcache::AppCacheBackend {
 public:
  explicit BrowserBackendProxy(BrowserAppCacheSystem* appcache_system)
      : system_(appcache_system), event_(true, false) {
    get_status_callback_.reset(
        NewCallback(this, &BrowserBackendProxy::GetStatusCallback));
    start_update_callback_.reset(
        NewCallback(this, &BrowserBackendProxy::StartUpdateCallback));
    swap_cache_callback_.reset(
        NewCallback(this, &BrowserBackendProxy::SwapCacheCallback));
  }

  virtual void RegisterHost(int host_id) {
    if (system_->is_ui_thread()) {
      system_->io_message_loop()->PostTask(FROM_HERE, NewRunnableMethod(
          this, &BrowserBackendProxy::RegisterHost, host_id));
    } else if (system_->is_io_thread()) {
      system_->backend_impl_->RegisterHost(host_id);
    } else {
      NOTREACHED();
    }
  }

  virtual void UnregisterHost(int host_id) {
    if (system_->is_ui_thread()) {
      system_->io_message_loop()->PostTask(FROM_HERE, NewRunnableMethod(
          this, &BrowserBackendProxy::UnregisterHost, host_id));
    } else if (system_->is_io_thread()) {
      system_->backend_impl_->UnregisterHost(host_id);
    } else {
      NOTREACHED();
    }
  }

  virtual void SelectCache(int host_id,
                           const GURL& document_url,
                           const int64 cache_document_was_loaded_from,
                           const GURL& manifest_url) {
    if (system_->is_ui_thread()) {
      system_->io_message_loop()->PostTask(FROM_HERE, NewRunnableMethod(
          this, &BrowserBackendProxy::SelectCache, host_id, document_url,
              cache_document_was_loaded_from, manifest_url));
    } else if (system_->is_io_thread()) {
      system_->backend_impl_->SelectCache(host_id, document_url,
                                          cache_document_was_loaded_from,
                                          manifest_url);
    } else {
      NOTREACHED();
    }
  }

  virtual void MarkAsForeignEntry(int host_id, const GURL& document_url,
                                  int64 cache_document_was_loaded_from) {
    if (system_->is_ui_thread()) {
      system_->io_message_loop()->PostTask(FROM_HERE, NewRunnableMethod(
          this, &BrowserBackendProxy::MarkAsForeignEntry, host_id, document_url,
          cache_document_was_loaded_from));
    } else if (system_->is_io_thread()) {
      system_->backend_impl_->MarkAsForeignEntry(
                                  host_id, document_url,
                                  cache_document_was_loaded_from);
    } else {
      NOTREACHED();
    }
  }

  virtual appcache::Status GetStatus(int host_id) {
    if (system_->is_ui_thread()) {
      status_result_ = appcache::UNCACHED;
      event_.Reset();
      system_->io_message_loop()->PostTask(FROM_HERE, NewRunnableMethod(
          this, &BrowserBackendProxy::GetStatus, host_id));
      event_.Wait();
    } else if (system_->is_io_thread()) {
      system_->backend_impl_->GetStatusWithCallback(
          host_id, get_status_callback_.get(), NULL);
    } else {
      NOTREACHED();
    }
    return status_result_;
  }

  virtual bool StartUpdate(int host_id) {
    if (system_->is_ui_thread()) {
      bool_result_ = false;
      event_.Reset();
      system_->io_message_loop()->PostTask(FROM_HERE, NewRunnableMethod(
          this, &BrowserBackendProxy::StartUpdate, host_id));
      event_.Wait();
    } else if (system_->is_io_thread()) {
      system_->backend_impl_->StartUpdateWithCallback(
          host_id, start_update_callback_.get(), NULL);
    } else {
      NOTREACHED();
    }
    return bool_result_;
  }

  virtual bool SwapCache(int host_id) {
    if (system_->is_ui_thread()) {
      bool_result_ = false;
      event_.Reset();
      system_->io_message_loop()->PostTask(FROM_HERE, NewRunnableMethod(
          this, &BrowserBackendProxy::SwapCache, host_id));
      event_.Wait();
    } else if (system_->is_io_thread()) {
      system_->backend_impl_->SwapCacheWithCallback(
          host_id, swap_cache_callback_.get(), NULL);
    } else {
      NOTREACHED();
    }
    return bool_result_;
  }

  void GetStatusCallback(appcache::Status status, void* param) {
    status_result_ = status;
    event_.Signal();
  }

  void StartUpdateCallback(bool result, void* param) {
    bool_result_ = result;
    event_.Signal();
  }

  void SwapCacheCallback(bool result, void* param) {
    bool_result_ = result;
    event_.Signal();
  }

  void SignalEvent() {
    event_.Signal();
  }

 private:
  BrowserAppCacheSystem* system_;
  base::WaitableEvent event_;
  bool bool_result_;
  appcache::Status status_result_;
  scoped_ptr<appcache::GetStatusCallback> get_status_callback_;
  scoped_ptr<appcache::StartUpdateCallback> start_update_callback_;
  scoped_ptr<appcache::SwapCacheCallback> swap_cache_callback_;
};


// BrowserAppCacheSystem --------------------------------------------------------

// This class only works for a single process browser.
static const int kSingleProcessId = 1;

// A not so thread safe singleton, but should work for test_shell.
BrowserAppCacheSystem* BrowserAppCacheSystem::instance_ = NULL;

BrowserAppCacheSystem::BrowserAppCacheSystem()
    : io_message_loop_(NULL), ui_message_loop_(NULL),
      ALLOW_THIS_IN_INITIALIZER_LIST(
          backend_proxy_(new BrowserBackendProxy(this))),
      ALLOW_THIS_IN_INITIALIZER_LIST(
          frontend_proxy_(new BrowserFrontendProxy(this))),
      backend_impl_(NULL), service_(NULL) {
  DCHECK(!instance_);
  instance_ = this;
}

BrowserAppCacheSystem::~BrowserAppCacheSystem() {
  DCHECK(!io_message_loop_ && !backend_impl_ && !service_);
  frontend_proxy_->clear_appcache_system();  // in case a task is in transit
  instance_ = NULL;
}

void BrowserAppCacheSystem::InitOnUIThread(
    const FilePath& cache_directory) {
  DCHECK(!ui_message_loop_);
  DCHECK(!cache_directory.empty());
  ui_message_loop_ = MessageLoop::current();
  cache_directory_ = cache_directory;
}

void BrowserAppCacheSystem::InitOnIOThread(URLRequestContext* request_context) {
  if (!is_initailized_on_ui_thread())
    return;

  DCHECK(!io_message_loop_);
  io_message_loop_ = MessageLoop::current();
  io_message_loop_->AddDestructionObserver(this);

  // Recreate and initialize per each IO thread.
  service_ = new appcache::AppCacheService();
  backend_impl_ = new appcache::AppCacheBackendImpl();
  service_->Initialize(cache_directory_);
  service_->set_request_context(request_context);
  backend_impl_->Initialize(service_, frontend_proxy_.get(), kSingleProcessId);

  AppCacheInterceptor::EnsureRegistered();
}

WebApplicationCacheHost* BrowserAppCacheSystem::CreateCacheHostForWebKit(
    WebApplicationCacheHostClient* client) {
  if (!is_initailized_on_ui_thread())
    return NULL;

  DCHECK(is_ui_thread());

  // The IO thread needs to be running for this system to work.
  BrowserResourceLoaderBridge::EnsureIOThread();

  if (!is_initialized())
    return NULL;
  return new WebApplicationCacheHostImpl(client, backend_proxy_.get());
}

void BrowserAppCacheSystem::SetExtraRequestBits(
    URLRequest* request, int host_id, ResourceType::Type resource_type) {
  if (is_initialized()) {
    DCHECK(is_io_thread());
    AppCacheInterceptor::SetExtraRequestInfo(
        request, service_, kSingleProcessId, host_id, resource_type);
  }
}

void BrowserAppCacheSystem::GetExtraResponseBits(
    URLRequest* request, int64* cache_id, GURL* manifest_url) {
  if (is_initialized()) {
    DCHECK(is_io_thread());
    AppCacheInterceptor::GetExtraResponseInfo(
        request, cache_id, manifest_url);
  }
}

void BrowserAppCacheSystem::WillDestroyCurrentMessageLoop() {
  DCHECK(is_io_thread());
  DCHECK(backend_impl_->hosts().empty());

  io_message_loop_ = NULL;
  delete backend_impl_;
  delete service_;
  backend_impl_ = NULL;
  service_ = NULL;

  // Just in case the main thread is waiting on it.
  backend_proxy_->SignalEvent();
}
