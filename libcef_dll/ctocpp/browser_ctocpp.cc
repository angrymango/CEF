// Copyright (c) 2009 The Chromium Embedded Framework Authors. All rights
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

#include "../precompiled_libcef.h"
#include "cpptoc/handler_cpptoc.h"
#include "ctocpp/browser_ctocpp.h"
#include "ctocpp/frame_ctocpp.h"


// STATIC METHODS - Body may be edited by hand.

bool CefBrowser::CreateBrowser(CefWindowInfo& windowInfo, bool popup,
    CefRefPtr<CefHandler> handler, const std::wstring& url)
{
  return cef_browser_create(&windowInfo, popup, CefHandlerCppToC::Wrap(handler),
      url.c_str());
}

CefRefPtr<CefBrowser> CefBrowser::CreateBrowserSync(CefWindowInfo& windowInfo,
    bool popup, CefRefPtr<CefHandler> handler, const std::wstring& url)
{
  cef_browser_t* impl = cef_browser_create_sync(&windowInfo, popup,
      CefHandlerCppToC::Wrap(handler), url.c_str());
  if(impl)
    return CefBrowserCToCpp::Wrap(impl);
  return NULL;
}


// VIRTUAL METHODS - Body may be edited by hand.

bool CefBrowserCToCpp::CanGoBack()
{
  if(CEF_MEMBER_MISSING(struct_, can_go_back))
    return false;

  return struct_->can_go_back(struct_) ? true : false;
}

void CefBrowserCToCpp::GoBack()
{
  if(CEF_MEMBER_MISSING(struct_, go_back))
    return;

  struct_->go_back(struct_);
}

bool CefBrowserCToCpp::CanGoForward()
{
  if(CEF_MEMBER_MISSING(struct_, can_go_forward))
    return false;
  
  return struct_->can_go_forward(struct_);
}

void CefBrowserCToCpp::GoForward()
{
  if(CEF_MEMBER_MISSING(struct_, go_forward))
    return;

  struct_->go_forward(struct_);
}

void CefBrowserCToCpp::Reload()
{
  if(CEF_MEMBER_MISSING(struct_, reload))
    return;

  struct_->reload(struct_);
}

void CefBrowserCToCpp::StopLoad()
{
  if(CEF_MEMBER_MISSING(struct_, stop_load))
    return;
  
  struct_->stop_load(struct_);
}

void CefBrowserCToCpp::SetFocus(bool enable)
{
  if(CEF_MEMBER_MISSING(struct_, set_focus))
    return;
  
  struct_->set_focus(struct_, enable);
}

CefWindowHandle CefBrowserCToCpp::GetWindowHandle()
{
  if(CEF_MEMBER_MISSING(struct_, get_window_handle))
      return 0;
  
  return struct_->get_window_handle(struct_);
}

bool CefBrowserCToCpp::IsPopup()
{
  if(CEF_MEMBER_MISSING(struct_, is_popup))
    return false;
  
  return struct_->is_popup(struct_);
}

CefRefPtr<CefHandler> CefBrowserCToCpp::GetHandler()
{
  if(CEF_MEMBER_MISSING(struct_, get_handler))
    return NULL;

  cef_handler_t* handlerStruct = struct_->get_handler(struct_);
  if(handlerStruct)
    return CefHandlerCppToC::Unwrap(handlerStruct);

  return NULL;
}

CefRefPtr<CefFrame> CefBrowserCToCpp::GetMainFrame()
{
  if(CEF_MEMBER_MISSING(struct_, get_main_frame))
    return NULL;

  cef_frame_t* frameStruct = struct_->get_main_frame(struct_);
  if(frameStruct)
    return CefFrameCToCpp::Wrap(frameStruct);

  return NULL;
}

CefRefPtr<CefFrame> CefBrowserCToCpp::GetFocusedFrame()
{
   if(CEF_MEMBER_MISSING(struct_, get_main_frame))
    return NULL;

  cef_frame_t* frameStruct = struct_->get_focused_frame(struct_);
  if(frameStruct)
    return CefFrameCToCpp::Wrap(frameStruct);

  return NULL;
}

CefRefPtr<CefFrame> CefBrowserCToCpp::GetFrame(const std::wstring& name)
{
  if(CEF_MEMBER_MISSING(struct_, get_main_frame))
    return NULL;

  cef_frame_t* frameStruct = struct_->get_frame(struct_, name.c_str());
  if(frameStruct)
    return CefFrameCToCpp::Wrap(frameStruct);

  return NULL;
}

void CefBrowserCToCpp::GetFrameNames(std::vector<std::wstring>& names)
{
  if(CEF_MEMBER_MISSING(struct_, get_frame_names))
    return;

  cef_string_list_t list = cef_string_list_alloc();
  struct_->get_frame_names(struct_, list);

  cef_string_t str;
  int size = cef_string_list_size(list);
  for(int i = 0; i < size; ++i) {
    str = cef_string_list_value(list, i);
    names.push_back(str);
    cef_string_free(str);
  }

  cef_string_list_free(list);
}


#ifdef _DEBUG
long CefCToCpp<CefBrowserCToCpp, CefBrowser, cef_browser_t>::DebugObjCt = 0;
#endif

