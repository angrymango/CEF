// Copyright (c) 2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <Cocoa/Cocoa.h>

#include "webview_host.h"
#include "browser_webview_delegate.h"
#include "browser_webview_mac.h"

#include "skia/ext/platform_canvas.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebSize.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebView.h"
#include "ui/gfx/rect.h"
#include "ui/gfx/size.h"
#include "webkit/glue/webpreferences.h"

using WebKit::WebDevToolsAgentClient;
using WebKit::WebSize;
using WebKit::WebView;

// static
WebViewHost* WebViewHost::Create(NSView* parent_view,
                                 const gfx::Rect& rect,
                                 BrowserWebViewDelegate* delegate,
                                 PaintDelegate* paint_delegate,
                                 WebDevToolsAgentClient* dev_tools_client,
                                 const WebPreferences& prefs) {
  WebViewHost* host = new WebViewHost();

  NSRect content_rect = {{rect.x(), rect.y()}, {rect.width(), rect.height()}};
  host->view_ = [[BrowserWebView alloc] initWithFrame:content_rect];
  // make the height and width track the window size.
  [host->view_ setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
  [parent_view addSubview:host->view_];
  [host->view_ release];

#if defined(WEBKIT_HAS_WEB_AUTO_FILL_CLIENT)
  host->webwidget_ = WebView::create(delegate, dev_tools_client, NULL);
#else
  host->webwidget_ = WebView::create(delegate, dev_tools_client);
#endif
  prefs.Apply(host->webview());
  host->webview()->initializeMainFrame(delegate);
  host->webwidget_->resize(WebSize(content_rect.size.width,
                                   content_rect.size.height));

  return host;
}

WebView* WebViewHost::webview() const {
  return static_cast<WebView*>(webwidget_);
}

void WebViewHost::SetIsActive(bool active) {
  webview()->setIsActive(active);
}
