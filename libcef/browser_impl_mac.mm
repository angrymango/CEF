// Copyright (c) 2010 The Chromium Embedded Framework Authors.
// Portions copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cef_context.h"
#include "browser_impl.h"
#include "browser_settings.h"
#include "browser_webview_mac.h"

#import <Cocoa/Cocoa.h>

#include "third_party/WebKit/Source/WebKit/chromium/public/WebFrame.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebRect.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebSize.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebView.h"
#include "webkit/glue/webpreferences.h"

using WebKit::WebRect;
using WebKit::WebSize;

CefWindowHandle CefBrowserImpl::GetWindowHandle()
{
  AutoLock lock_scope(this);
  return window_info_.m_View;
}

gfx::NativeView CefBrowserImpl::UIT_GetMainWndHandle() const {
  REQUIRE_UIT();
  return window_info_.m_View;
}

void CefBrowserImpl::UIT_CreateBrowser(const CefString& url)
{
  REQUIRE_UIT();
  Lock();

  // Add a reference that will be released in UIT_DestroyBrowser().
  AddRef();

  // Add the new browser to the list maintained by the context
  _Context->AddBrowser(this);

  if (!settings_.developer_tools_disabled)
    dev_tools_agent_.reset(new BrowserDevToolsAgent());

  NSWindow* newWnd = nil;

  NSView* parentView = window_info_.m_ParentView;
  gfx::Rect contentRect(window_info_.m_x, window_info_.m_y,
                        window_info_.m_nWidth, window_info_.m_nHeight);
  if (parentView == nil) {
    // Create a new window.
    NSRect screen_rect = [[NSScreen mainScreen] visibleFrame];
    NSRect window_rect = {{window_info_.m_x,
                           screen_rect.size.height - window_info_.m_y},
                          {window_info_.m_nWidth, window_info_.m_nHeight}};
    if (window_rect.size.width == 0)
      window_rect.size.width = 500;
    if (window_rect.size.height == 0)
      window_rect.size.height = 500;
    contentRect.SetRect(0, 0, window_rect.size.width, window_rect.size.height);

    newWnd = [[NSWindow alloc]
              initWithContentRect:window_rect
              styleMask:(NSTitledWindowMask |
                         NSClosableWindowMask |
                         NSMiniaturizableWindowMask |
                         NSResizableWindowMask )
              backing:NSBackingStoreBuffered
              defer:NO];
    parentView = [newWnd contentView];
    window_info_.m_ParentView = parentView;
  }

  WebPreferences prefs;
  BrowserToWebSettings(settings_, prefs);

  // Create the webview host object
  webviewhost_.reset(
      WebViewHost::Create(parentView, contentRect, delegate_.get(),
                          dev_tools_agent_.get(), prefs));
  delegate_->RegisterDragDrop();

  if (!settings_.developer_tools_disabled)
    dev_tools_agent_->SetWebView(webviewhost_->webview());

  BrowserWebView* browserView = (BrowserWebView*)webviewhost_->view_handle();
  browserView.browser = this;
  window_info_.m_View = browserView;

  Unlock();

  if (newWnd != nil) {
    // Show the window.
    [newWnd makeKeyAndOrderFront: nil];
  }

  if(handler_.get()) {
    // Notify the handler that we're done creating the new window
    handler_->HandleAfterCreated(this);
  }

  if(url.size() > 0)
    UIT_LoadURL(GetMainFrame(), url);
}

void CefBrowserImpl::UIT_SetFocus(WebWidgetHost* host, bool enable)
{
  REQUIRE_UIT();
  if (!host)
    return;

  NSView* view = host->view_handle();
  if (!view)
    return;

  if (enable)
    [[view window] makeFirstResponder:view];
}

bool CefBrowserImpl::UIT_ViewDocumentString(WebKit::WebFrame *frame)
{
  REQUIRE_UIT();

  // TODO(port): Add implementation.
  NOTIMPLEMENTED();
  return false;
}

void CefBrowserImpl::UIT_PrintPage(int page_number, int total_pages,
                                   const gfx::Size& canvas_size,
                                   WebKit::WebFrame* frame) {
  REQUIRE_UIT();

  // TODO(port): Add implementation.
  NOTIMPLEMENTED();
}

void CefBrowserImpl::UIT_PrintPages(WebKit::WebFrame* frame) {
  REQUIRE_UIT();

  // TODO(port): Add implementation.
  NOTIMPLEMENTED();
}

int CefBrowserImpl::UIT_GetPagesCount(WebKit::WebFrame* frame)
{
  REQUIRE_UIT();

  // TODO(port): Add implementation.
  NOTIMPLEMENTED();
  return 0;
}

// static
void CefBrowserImpl::UIT_CloseView(gfx::NativeView view)
{
  [view performSelector:@selector(performClose:) withObject:nil afterDelay:0];
}
