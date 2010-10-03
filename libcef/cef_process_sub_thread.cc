// Copyright (c) 2010 The Chromium Embedded Framework Authors.
// Portions copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cef_process_sub_thread.h"
#include "build/build_config.h"

#if defined(OS_WIN)
#include <Objbase.h>
#endif

CefProcessSubThread::CefProcessSubThread(CefThread::ID identifier)
      : CefThread(identifier) {}

CefProcessSubThread::CefProcessSubThread(CefThread::ID identifier,
                                         MessageLoop* message_loop)
      : CefThread(identifier, message_loop) {}

CefProcessSubThread::~CefProcessSubThread() {
  // We cannot rely on our base class to stop the thread since we want our
  // CleanUp function to run.
  Stop();
}

void CefProcessSubThread::Init() {
#if defined(OS_WIN)
  // Initializes the COM library on the current thread.
  CoInitialize(NULL);
#endif
}

void CefProcessSubThread::CleanUp() {
  // Flush any remaining messages.  This ensures that any accumulated
  // Task objects get destroyed before we exit, which avoids noise in
  // purify leak-test results.
  MessageLoop::current()->RunAllPending();

#if defined(OS_WIN)
  // Closes the COM library on the current thread. CoInitialize must
  // be balanced by a corresponding call to CoUninitialize.
  CoUninitialize();
#endif
}
