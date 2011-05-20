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

#include "libcef_dll/ctocpp/cookie_visitor_ctocpp.h"


// VIRTUAL METHODS - Body may be edited by hand.

bool CefCookieVisitorCToCpp::Visit(const CefCookie& cookie, int count,
    int total, bool& deleteCookie)
{
  if(CEF_MEMBER_MISSING(struct_, visit))
    return false;

  int delVal = deleteCookie;
  bool retVal = struct_->visit(struct_, &cookie, count, total, &delVal) ?
      true : false;
  deleteCookie = delVal?true:false;

  return retVal;
}


#ifndef NDEBUG
template<> long CefCToCpp<CefCookieVisitorCToCpp, CefCookieVisitor,
    cef_cookie_visitor_t>::DebugObjCt = 0;
#endif

