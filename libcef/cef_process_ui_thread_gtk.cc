// Copyright (c) 2011 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "cef_process_ui_thread.h"
#include "browser_impl.h"
#include "browser_webkit_glue.h"

void CefProcessUIThread::PlatformInit() {
	webkit_glue::InitializeDataPak();
}

void CefProcessUIThread::PlatformCleanUp() {
  
}

