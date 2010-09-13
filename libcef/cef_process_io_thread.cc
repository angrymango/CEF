// Copyright (c) 2010 The Chromium Embedded Framework Authors.
// Portions copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cef_process_io_thread.h"
#include "cef_context.h"
#include "browser_appcache_system.h"
#include "browser_resource_loader_bridge.h"
#include "browser_socket_stream_bridge.h"
#include "browser_webblobregistry_impl.h"

#include "build/build_config.h"

#if defined(OS_WIN)
#include <Objbase.h>
#endif

CefProcessIOThread::CefProcessIOThread()
      : CefThread(CefThread::IO), request_context_(NULL) {}

CefProcessIOThread::CefProcessIOThread(MessageLoop* message_loop)
      : CefThread(CefThread::IO, message_loop), request_context_(NULL) {}

CefProcessIOThread::~CefProcessIOThread() {
  // We cannot rely on our base class to stop the thread since we want our
  // CleanUp function to run.
  Stop();
}

void CefProcessIOThread::Init() {
#if defined(OS_WIN)
  // Initializes the COM library on the current thread.
  CoInitialize(NULL);
#endif

  request_context_ = new BrowserRequestContext(FilePath(_Context->cache_path()),
      net::HttpCache::NORMAL, false);
  _Context->set_request_context(request_context_);

  BrowserAppCacheSystem::InitializeOnIOThread(request_context_);
  BrowserSocketStreamBridge::InitializeOnIOThread(request_context_);
  BrowserWebBlobRegistryImpl::InitializeOnIOThread(
      request_context_->blob_storage_controller());
}

void CefProcessIOThread::CleanUp() {
  // Flush any remaining messages.  This ensures that any accumulated
  // Task objects get destroyed before we exit, which avoids noise in
  // purify leak-test results.
  MessageLoop::current()->RunAllPending();

  BrowserSocketStreamBridge::Cleanup();
  BrowserWebBlobRegistryImpl::Cleanup();

  _Context->set_request_context(NULL);
  request_context_ = NULL;

#if defined(OS_WIN)
  // Closes the COM library on the current thread. CoInitialize must
  // be balanced by a corresponding call to CoUninitialize.
  CoUninitialize();
#endif
}
