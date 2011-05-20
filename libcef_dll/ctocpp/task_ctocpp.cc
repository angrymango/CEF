// Copyright (c) 2011 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.
//
// ---------------------------------------------------------------------------
//
// A portion of this file was generated by the CEF translator tool.  When
// making changes by hand only do so within the body of existing static and
// virtual method implementations. See the translator.README.txt file in the
// tools directory for more information.
//

#include "libcef_dll/ctocpp/task_ctocpp.h"


// VIRTUAL METHODS - Body may be edited by hand.

void CefTaskCToCpp::Execute(CefThreadId threadId)
{
  if(CEF_MEMBER_MISSING(struct_, execute))
   return;

  struct_->execute(struct_, threadId);
}


#ifndef NDEBUG
template<> long CefCToCpp<CefTaskCToCpp, CefTask, cef_task_t>::DebugObjCt = 0;
#endif

