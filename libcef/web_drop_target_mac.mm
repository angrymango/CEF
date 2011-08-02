// Copyright (c) 2011 The Chromium Embedded Framework Authors.
// Portions copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "browser_impl.h"
#import "browser_webview_mac.h"
#include "cef_context.h"
#import "web_drop_target_mac.h"
#import "web_drag_utils_mac.h"

#include "base/logging.h"
#include "base/sys_string_conversions.h"
#import "third_party/mozilla/NSPasteboard+Utils.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebDragData.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebDragOperation.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebPoint.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebView.h"
#include "webkit/glue/webdropdata.h"
#include "webkit/glue/window_open_disposition.h"

using WebKit::WebDragOperationsMask;
using WebKit::WebPoint;
using WebKit::WebView;

@implementation WebDropTarget

// |view| is the WebView representing this browser window, used to communicate
// drag&drop messages to WebCore and handle navigation on a successful drop
// (if necessary).
- (id)initWithWebView:(BrowserWebView*)view {
  if ((self = [super init]))
    view_ = view;
  return self;
}

// Call to set whether or not we should allow the drop. Takes effect the
// next time |-draggingUpdated:| is called.
- (void)setCurrentOperation: (NSDragOperation)operation {
  current_operation_ = operation;
}

// Given a point in window coordinates and a view in that window, return a
// flipped point in the coordinate system of |view|.
- (NSPoint)flipWindowPointToView:(const NSPoint&)windowPoint
                            view:(NSView*)view {
  DCHECK(view);
  NSPoint viewPoint =  [view convertPoint:windowPoint fromView:nil];
  NSRect viewFrame = [view frame];
  viewPoint.y = viewFrame.size.height - viewPoint.y;
  return viewPoint;
}

// Given a point in window coordinates and a view in that window, return a
// flipped point in screen coordinates.
- (NSPoint)flipWindowPointToScreen:(const NSPoint&)windowPoint
                              view:(NSView*)view {
  DCHECK(view);
  NSPoint screenPoint = [[view window] convertBaseToScreen:windowPoint];
  NSScreen* screen = [[view window] screen];
  NSRect screenFrame = [screen frame];
  screenPoint.y = screenFrame.size.height - screenPoint.y;
  return screenPoint;
}

// Return YES if the drop site only allows drops that would navigate.  If this
// is the case, we don't want to pass messages to the renderer because there's
// really no point (i.e., there's nothing that cares about the mouse position or
// entering and exiting).  One example is an interstitial page (e.g., safe
// browsing warning).
- (BOOL)onlyAllowsNavigation {
  return false;
}

// Messages to send during the tracking of a drag, usually upon receiving
// calls from the view system. Communicates the drag messages to WebCore.

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)info
                              view:(NSView*)view {
  // Save off the current WebViewHost so we can tell if it changes during a
  // drag. If it does, we need to send a new enter message in draggingUpdated:.
  current_wvh_ = _Context->current_webviewhost();
  DCHECK(current_wvh_);

  if ([self onlyAllowsNavigation]) {
    if ([[info draggingPasteboard] containsURLData])
      return NSDragOperationCopy;
    return NSDragOperationNone;
  }
  
  WebView* webview = view_.browser->UIT_GetWebView();

  // Fill out a WebDropData from pasteboard.
  WebDropData data;
  [self populateWebDropData:&data fromPasteboard:[info draggingPasteboard]];

  // Create the appropriate mouse locations for WebCore. The draggingLocation
  // is in window coordinates. Both need to be flipped.
  NSPoint windowPoint = [info draggingLocation];
  NSPoint viewPoint = [self flipWindowPointToView:windowPoint view:view];
  NSPoint screenPoint = [self flipWindowPointToScreen:windowPoint view:view];
  NSDragOperation mask = [info draggingSourceOperationMask];
  webview->dragTargetDragEnter(data.ToDragData(),
                               WebPoint(viewPoint.x, viewPoint.y),
                               WebPoint(screenPoint.x, screenPoint.y),
                               static_cast<WebDragOperationsMask>(mask));

  // We won't know the true operation (whether the drag is allowed) until we
  // hear back from the renderer. For now, be optimistic:
  current_operation_ = NSDragOperationCopy;
  return current_operation_;
}

- (void)draggingExited:(id<NSDraggingInfo>)info {
  DCHECK(current_wvh_);
  if (current_wvh_ != _Context->current_webviewhost())
    return;

  WebView* webview = view_.browser->UIT_GetWebView();
  
  // Nothing to do in the interstitial case.

  webview->dragTargetDragLeave();
}

- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)info
                              view:(NSView*)view {
  DCHECK(current_wvh_);
  if (current_wvh_ != _Context->current_webviewhost())
    [self draggingEntered:info view:view];

  if ([self onlyAllowsNavigation]) {
    if ([[info draggingPasteboard] containsURLData])
      return NSDragOperationCopy;
    return NSDragOperationNone;
  }

  WebView* webview = view_.browser->UIT_GetWebView();
  
  // Create the appropriate mouse locations for WebCore. The draggingLocation
  // is in window coordinates.
  NSPoint windowPoint = [info draggingLocation];
  NSPoint viewPoint = [self flipWindowPointToView:windowPoint view:view];
  NSPoint screenPoint = [self flipWindowPointToScreen:windowPoint view:view];
  NSDragOperation mask = [info draggingSourceOperationMask];
  webview->dragTargetDragOver(WebPoint(viewPoint.x, viewPoint.y),
                              WebPoint(screenPoint.x, screenPoint.y),
                              static_cast<WebDragOperationsMask>(mask));

  return current_operation_;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)info
                              view:(NSView*)view {
  DCHECK(current_wvh_);
  if (current_wvh_ != _Context->current_webviewhost())
    [self draggingEntered:info view:view];

  // Check if we only allow navigation and navigate to a url on the pasteboard.
  if ([self onlyAllowsNavigation]) {
    NSPasteboard* pboard = [info draggingPasteboard];
    if ([pboard containsURLData]) {
      GURL url;
      drag_util::PopulateURLAndTitleFromPasteBoard(&url, NULL, pboard, YES);
      view_.browser->GetMainFrame()->LoadURL(url.spec());
      return YES;
    }
    return NO;
  }

  current_wvh_ = NULL;

  WebView* webview = view_.browser->UIT_GetWebView();
  
  // Create the appropriate mouse locations for WebCore. The draggingLocation
  // is in window coordinates. Both need to be flipped.
  NSPoint windowPoint = [info draggingLocation];
  NSPoint viewPoint = [self flipWindowPointToView:windowPoint view:view];
  NSPoint screenPoint = [self flipWindowPointToScreen:windowPoint view:view];
  webview->dragTargetDrop(gfx::Point(viewPoint.x, viewPoint.y),
                          gfx::Point(screenPoint.x, screenPoint.y));

  return YES;
}

// Given |data|, which should not be nil, fill it in using the contents of the
// given pasteboard.
- (void)populateWebDropData:(WebDropData*)data
             fromPasteboard:(NSPasteboard*)pboard {
  DCHECK(data);
  DCHECK(pboard);
  NSArray* types = [pboard types];

  // Get URL if possible. To avoid exposing file system paths to web content,
  // filenames in the drag are not converted to file URLs.
  drag_util::PopulateURLAndTitleFromPasteBoard(&data->url,
                                               &data->url_title,
                                               pboard,
                                               NO);

  // Get plain text.
  if ([types containsObject:NSStringPboardType]) {
    data->plain_text =
        base::SysNSStringToUTF16([pboard stringForType:NSStringPboardType]);
  }

  // Get HTML. If there's no HTML, try RTF.
  if ([types containsObject:NSHTMLPboardType]) {
    data->text_html =
        base::SysNSStringToUTF16([pboard stringForType:NSHTMLPboardType]);
  } else if ([types containsObject:NSRTFPboardType]) {
    NSString* html = [pboard htmlFromRtf];
    data->text_html = base::SysNSStringToUTF16(html);
  }

  // Get files.
  if ([types containsObject:NSFilenamesPboardType]) {
    NSArray* files = [pboard propertyListForType:NSFilenamesPboardType];
    if ([files isKindOfClass:[NSArray class]] && [files count]) {
      for (NSUInteger i = 0; i < [files count]; i++) {
        NSString* filename = [files objectAtIndex:i];
        BOOL isDir = NO;
        BOOL exists = [[NSFileManager defaultManager] fileExistsAtPath:filename
                                                           isDirectory:&isDir];
        if (exists && !isDir)
          data->filenames.push_back(base::SysNSStringToUTF16(filename));
      }
    }
  }

  // TODO(pinkerton): Get file contents. http://crbug.com/34661
}

@end
