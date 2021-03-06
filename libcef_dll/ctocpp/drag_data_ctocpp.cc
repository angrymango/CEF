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

#include "libcef_dll/ctocpp/drag_data_ctocpp.h"
#include "libcef_dll/transfer_util.h"


// VIRTUAL METHODS - Body may be edited by hand.

bool CefDragDataCToCpp::IsLink()
{
  if (CEF_MEMBER_MISSING(struct_, is_link))
    return false;

  return struct_->is_link(struct_) ? true : false;
}

bool CefDragDataCToCpp::IsFragment()
{
  if (CEF_MEMBER_MISSING(struct_, is_fragment))
    return false;

  return struct_->is_fragment(struct_) ? true : false;
}

bool CefDragDataCToCpp::IsFile()
{
  if (CEF_MEMBER_MISSING(struct_, is_file))
    return false;

  return struct_->is_file(struct_) ? true : false;
}

CefString CefDragDataCToCpp::GetLinkURL()
{
  CefString str;
  if (CEF_MEMBER_MISSING(struct_, get_link_url))
    return str;

  cef_string_userfree_t strPtr = struct_->get_link_url(struct_);
  str.AttachToUserFree(strPtr);
  return str;
}

CefString CefDragDataCToCpp::GetLinkTitle()
{
  CefString str;
  if (CEF_MEMBER_MISSING(struct_, get_link_title))
    return str;

  cef_string_userfree_t strPtr = struct_->get_link_title(struct_);
  str.AttachToUserFree(strPtr);
  return str;
}

CefString CefDragDataCToCpp::GetLinkMetadata()
{
  CefString str;
  if (CEF_MEMBER_MISSING(struct_, get_link_metadata))
    return str;

  cef_string_userfree_t strPtr = struct_->get_link_metadata(struct_);
  str.AttachToUserFree(strPtr);
  return str;
}

CefString CefDragDataCToCpp::GetFragmentText()
{
  CefString str;
  if (CEF_MEMBER_MISSING(struct_, get_fragment_text))
    return str;

  cef_string_userfree_t strPtr = struct_->get_fragment_text(struct_);
  str.AttachToUserFree(strPtr);
  return str;
}

CefString CefDragDataCToCpp::GetFragmentHtml()
{
  CefString str;
  if (CEF_MEMBER_MISSING(struct_, get_fragment_html))
    return str;

  cef_string_userfree_t strPtr = struct_->get_fragment_html(struct_);
  str.AttachToUserFree(strPtr);
  return str;
}

CefString CefDragDataCToCpp::GetFragmentBaseURL()
{
  CefString str;
  if (CEF_MEMBER_MISSING(struct_, get_fragment_base_url))
    return str;

  cef_string_userfree_t strPtr = struct_->get_fragment_base_url(struct_);
  str.AttachToUserFree(strPtr);
  return str;
}

CefString CefDragDataCToCpp::GetFileExtension()
{
  CefString str;
  if (CEF_MEMBER_MISSING(struct_, get_file_extension))
    return str;

  cef_string_userfree_t strPtr = struct_->get_file_extension(struct_);
  str.AttachToUserFree(strPtr);
  return str;
}

CefString CefDragDataCToCpp::GetFileName()
{
  CefString str;
  if (CEF_MEMBER_MISSING(struct_, get_file_name))
    return str;

  cef_string_userfree_t strPtr = struct_->get_file_name(struct_);
  str.AttachToUserFree(strPtr);
  return str;
}

bool CefDragDataCToCpp::GetFileNames(std::vector<CefString>& names)
{
  if (CEF_MEMBER_MISSING(struct_, get_file_names))
    return false;

  cef_string_list_t list = cef_string_list_alloc();
  if(struct_->get_file_names(struct_, list)) {
    transfer_string_list_contents(list, names);
    cef_string_list_free(list);
    return true;
  }

  return false;
}


#ifndef NDEBUG
template<> long CefCToCpp<CefDragDataCToCpp, CefDragData,
    cef_drag_data_t>::DebugObjCt = 0;
#endif

