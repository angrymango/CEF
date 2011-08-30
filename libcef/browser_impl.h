// Copyright (c) 2008-2009 The Chromium Embedded Framework Authors.
// Portions copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _BROWSER_IMPL_H
#define _BROWSER_IMPL_H

#include "include/cef.h"

#include "webview_host.h"
#include "browser_devtools_agent.h"
#include "browser_devtools_client.h"
#include "browser_webview_delegate.h"
#include "browser_navigation_controller.h"
#include "cef_thread.h"
#include "tracker.h"
#if defined(OS_WIN)
#include "printing/win_printing_context.h"
#endif

#include "base/scoped_temp_dir.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebFindOptions.h"

namespace base {
class WaitableEvent;
}
namespace WebKit {
class WebView;
}

#define BUFFER_SIZE   32768


// Implementation of CefBrowser.
class CefBrowserImpl : public CefBrowser
{
public:
  class PaintDelegate : public WebWidgetHost::PaintDelegate
  {
  public:
    PaintDelegate(CefBrowserImpl* browser);
    virtual ~PaintDelegate();

    virtual void Paint(bool popup, const gfx::Rect& dirtyRect,
                       const void* buffer);

  protected:
    CefBrowserImpl* browser_;
  };

  CefBrowserImpl(const CefWindowInfo& windowInfo,
                 const CefBrowserSettings& settings,
                 gfx::NativeView opener,
                 CefRefPtr<CefClient> client);
  virtual ~CefBrowserImpl() {}

#if defined(OS_WIN)
  static LPCTSTR GetWndClass();
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam,
                                  LPARAM lParam);
#endif

  // CefBrowser methods
  virtual void CloseBrowser() OVERRIDE;
  virtual bool CanGoBack() OVERRIDE { return can_go_back(); }
  virtual void GoBack() OVERRIDE;
  virtual bool CanGoForward() OVERRIDE { return can_go_forward(); }
  virtual void GoForward() OVERRIDE;
  virtual void Reload() OVERRIDE;
  virtual void ReloadIgnoreCache() OVERRIDE;
  virtual void StopLoad() OVERRIDE;
  virtual void SetFocus(bool enable) OVERRIDE;
  virtual void ParentWindowWillClose() OVERRIDE;
  virtual CefWindowHandle GetWindowHandle() OVERRIDE;
  virtual CefWindowHandle GetOpenerWindowHandle() OVERRIDE
      { return opener_window(); }
  virtual bool IsPopup() OVERRIDE { return is_popup(); }
  virtual bool HasDocument() OVERRIDE { return has_document(); }
  virtual CefRefPtr<CefClient> GetClient() OVERRIDE { return client_; }
  virtual CefRefPtr<CefFrame> GetMainFrame() OVERRIDE
      { return GetMainCefFrame(); }
  virtual CefRefPtr<CefFrame> GetFocusedFrame() OVERRIDE;
  virtual CefRefPtr<CefFrame> GetFrame(const CefString& name) OVERRIDE;
  virtual void GetFrameNames(std::vector<CefString>& names) OVERRIDE;
  virtual void Find(int identifier, const CefString& searchText,
                    bool forward, bool matchCase, bool findNext) OVERRIDE;
  virtual void StopFinding(bool clearSelection) OVERRIDE;
  virtual double GetZoomLevel() OVERRIDE { return zoom_level(); }
  virtual void SetZoomLevel(double zoomLevel) OVERRIDE;
  virtual void ShowDevTools() OVERRIDE;
  virtual void CloseDevTools() OVERRIDE;
  virtual bool IsWindowRenderingDisabled() OVERRIDE;
  virtual bool GetSize(PaintElementType type, int& width, int& height) OVERRIDE;
  virtual void SetSize(PaintElementType type, int width, int height) OVERRIDE;
  virtual bool IsPopupVisible() OVERRIDE;
  virtual void HidePopup() OVERRIDE;
  virtual void Invalidate(const CefRect& dirtyRect) OVERRIDE;
  virtual bool GetImage(PaintElementType type, int width, int height,
                        void* buffer) OVERRIDE;
  virtual void SendKeyEvent(KeyType type, int key, int modifiers, bool sysChar,
                            bool imeChar) OVERRIDE;
  virtual void SendMouseClickEvent(int x, int y, MouseButtonType type,
                                   bool mouseUp, int clickCount) OVERRIDE;
  virtual void SendMouseMoveEvent(int x, int y, bool mouseLeave) OVERRIDE;
  virtual void SendMouseWheelEvent(int x, int y, int delta) OVERRIDE;
  virtual void SendFocusEvent(bool setFocus) OVERRIDE;
  virtual void SendCaptureLostEvent() OVERRIDE;

  // Frame-related methods
  void Undo(CefRefPtr<CefFrame> frame);
  void Redo(CefRefPtr<CefFrame> frame);
  void Cut(CefRefPtr<CefFrame> frame);
  void Copy(CefRefPtr<CefFrame> frame);
  void Paste(CefRefPtr<CefFrame> frame);
  void Delete(CefRefPtr<CefFrame> frame);
  void SelectAll(CefRefPtr<CefFrame> frame);
  void Print(CefRefPtr<CefFrame> frame);
  void ViewSource(CefRefPtr<CefFrame> frame);
  CefString GetSource(CefRefPtr<CefFrame> frame);
  CefString GetText(CefRefPtr<CefFrame> frame);
  void LoadRequest(CefRefPtr<CefFrame> frame,
                   CefRefPtr<CefRequest> request);
  void LoadURL(CefRefPtr<CefFrame> frame,
               const CefString& url);
  void LoadString(CefRefPtr<CefFrame> frame,
                  const CefString& string,
                  const CefString& url);
  void LoadStream(CefRefPtr<CefFrame> frame,
                  CefRefPtr<CefStreamReader> stream,
                  const CefString& url);
  void ExecuteJavaScript(CefRefPtr<CefFrame> frame,
                         const CefString& jsCode, 
                         const CefString& scriptUrl,
                         int startLine);
  CefString GetURL(CefRefPtr<CefFrame> frame);

  // CefFrames are light-weight objects managed by the browser and loosely
  // coupled to a WebFrame object by name.  If a CefFrame object does not
  // already exist for the specified name one will be created.  There is no
  // guarantee that the same CefFrame object will be returned across different
  // calls to this function.
  CefRefPtr<CefFrame> GetCefFrame(const CefString& name);
  void RemoveCefFrame(const CefString& name);
  CefRefPtr<CefFrame> GetMainCefFrame();

  ////////////////////////////////////////////////////////////
  // ALL UIT_* METHODS MUST ONLY BE CALLED ON THE UI THREAD //
  ////////////////////////////////////////////////////////////

  CefRefPtr<CefFrame> UIT_GetCefFrame(WebKit::WebFrame* frame);
  
  // Return the main WebFrame object.
  WebKit::WebFrame* UIT_GetMainWebFrame();

  // Return the WebFrame object associated with the specified CefFrame.  This
  // may return NULL if no WebFrame with the CefFrame's name exists.
  WebKit::WebFrame* UIT_GetWebFrame(CefRefPtr<CefFrame> frame);

  WebKit::WebView* UIT_GetWebView() const {
    REQUIRE_UIT();
    return webviewhost_.get() ? webviewhost_->webview() : NULL;
  }
  WebViewHost* UIT_GetWebViewHost() const {
    REQUIRE_UIT();
    return webviewhost_.get();
  }
  BrowserWebViewDelegate* UIT_GetWebViewDelegate() const {
    REQUIRE_UIT();
    return delegate_.get();
  }
  gfx::NativeView UIT_GetWebViewWndHandle() {
    REQUIRE_UIT();
    DCHECK(!IsWindowRenderingDisabled());
    return webviewhost_->view_handle();
  }
  WebKit::WebWidget* UIT_GetPopup() const {
    REQUIRE_UIT();
    return popuphost_ ? popuphost_->webwidget() : NULL;
  }
  WebWidgetHost* UIT_GetPopupHost() const {
    REQUIRE_UIT();
    return popuphost_;
  }
  BrowserWebViewDelegate* UIT_GetPopupDelegate() const {
    REQUIRE_UIT();
    return popup_delegate_.get();
  }
  gfx::NativeView UIT_GetPopupWndHandle() {
    REQUIRE_UIT();
    DCHECK(!IsWindowRenderingDisabled());
    return popuphost_->view_handle();
  }
  gfx::NativeView UIT_GetMainWndHandle();

  BrowserNavigationController* UIT_GetNavigationController() {
    REQUIRE_UIT();
    return nav_controller_.get();
  }
  
  // Return true to allow user editing such as entering text in form elements
  bool UIT_AllowEditing() { return true; }

  bool UIT_IsModal() {
    REQUIRE_UIT();
    return is_modal_;
  }
  void UIT_SetIsModal(bool val) {
    REQUIRE_UIT();
    is_modal_ = val;
  }

  CefString UIT_GetTitle() {
    REQUIRE_UIT();
    return title_;
  }
  void UIT_SetTitle(const CefString& title) {
    REQUIRE_UIT();
    title_ = title;
  }

  // Create the native browser window and populate browser members.
  void UIT_CreateBrowser(const CefString& url);

  // Destroy the browser members. This method should only be called after the
  // native browser window is not longer processing messages.
  void UIT_DestroyBrowser();

  // Sends a message via the OS to close the native browser window.
  // UIT_DestroyBrowser will be called after the native window has closed.
  void UIT_CloseBrowser();

  void UIT_LoadURL(CefRefPtr<CefFrame> frame,
                   const CefString& url);
  void UIT_LoadURLForRequest(CefRefPtr<CefFrame> frame,
                             const CefString& url,
                             const CefString& method,
                             const WebKit::WebHTTPBody& upload_data,
                             const CefRequest::HeaderMap& headers);
  void UIT_LoadURLForRequestRef(CefRefPtr<CefFrame> frame,
                                CefRefPtr<CefRequest> request);
  void UIT_LoadHTML(CefRefPtr<CefFrame> frame,
                    const CefString& html,
                    const CefString& url);
  void UIT_LoadHTMLForStreamRef(CefRefPtr<CefFrame> frame,
                                CefRefPtr<CefStreamReader> stream,
                                const CefString& url);
  void UIT_ExecuteJavaScript(CefRefPtr<CefFrame> frame,
                             const CefString& js_code, 
                             const CefString& script_url,
                             int start_line);
  void UIT_GoBackOrForward(int offset);
  void UIT_Reload(bool ignoreCache);
  bool UIT_Navigate(const BrowserNavigationEntry& entry,
                    bool reload,
                    bool ignoreCahce);
  void UIT_SetFocus(WebWidgetHost* host, bool enable);
  void UIT_SetSize(PaintElementType type, int width, int height);
  void UIT_Invalidate(const CefRect& dirtyRect);
  void UIT_SendKeyEvent(KeyType type, int key, int modifiers, bool sysChar,
                        bool imeChar);
  void UIT_SendMouseClickEvent(int x, int y, MouseButtonType type,
                               bool mouseUp, int clickCount);
  void UIT_SendMouseMoveEvent(int x, int y, bool mouseLeave);
  void UIT_SendMouseWheelEvent(int x, int y, int delta);
  void UIT_SendFocusEvent(bool setFocus);
  void UIT_SendCaptureLostEvent();

  CefRefPtr<CefBrowserImpl> UIT_CreatePopupWindow(const CefString& url,
      const CefPopupFeatures& features);
  WebKit::WebWidget* UIT_CreatePopupWidget();
  void UIT_ClosePopupWidget();

  void UIT_Show(WebKit::WebNavigationPolicy policy);
  
  // Handles most simple browser actions
  void UIT_HandleActionView(cef_handler_menuid_t menuId);
  void UIT_HandleAction(cef_handler_menuid_t menuId, CefRefPtr<CefFrame> frame);

  // Save the document HTML to a temporary file and open in the default viewing
  // application
  bool UIT_ViewDocumentString(WebKit::WebFrame *frame);

  bool UIT_CanGoBack() { return !nav_controller_->IsAtStart(); }
  bool UIT_CanGoForward() { return !nav_controller_->IsAtEnd(); }

  // Printing support
  void UIT_PrintPages(WebKit::WebFrame* frame);
  void UIT_PrintPage(int page_number, int total_pages,
                     const gfx::Size& canvas_size, WebKit::WebFrame* frame);
  int UIT_GetPagesCount(WebKit::WebFrame* frame);

  void UIT_SetUniqueID(int id) { unique_id_ = id; }
  int UIT_GetUniqueID() { return unique_id_; }

  void UIT_Find(int identifier, const CefString& search_text,
                const WebKit::WebFindOptions& options);
  void UIT_StopFinding(bool clear_selection);
  void UIT_NotifyFindStatus(int identifier, int count,
                            const WebKit::WebRect& selection_rect,
                            int active_match_ordinal, bool final_update);
  void UIT_SetZoomLevel(double zoomLevel);
  void UIT_ShowDevTools();
  void UIT_CloseDevTools();

  void UIT_VisitDOM(CefRefPtr<CefFrame> frame,
                    CefRefPtr<CefDOMVisitor> visitor);

  // Frame objects will be deleted immediately before the frame is closed.
  void UIT_AddFrameObject(WebKit::WebFrame* frame,
                                 CefTrackObject* tracked_object);
  void UIT_BeforeFrameClosed(WebKit::WebFrame* frame);

  // These variables are read-only.
  const CefBrowserSettings& settings() const { return settings_; }
  const FilePath& file_system_root() const { return file_system_root_.path(); }
  gfx::NativeView opener_window() { return opener_; }
  bool is_popup() { return (opener_ != NULL); }
  
  // These variables may be read/written from multiple threads.
  void set_zoom_level(double zoomLevel);
  double zoom_level();
  void set_nav_state(bool can_go_back, bool can_go_forward);
  bool can_go_back();
  bool can_go_forward();
  void set_has_document(bool has_document);
  bool has_document();

#if defined(OS_WIN)
  void set_opener_was_disabled_by_modal_loop(bool disabled)
  {
    opener_was_disabled_by_modal_loop_ = disabled;
  }
  void set_internal_modal_message_loop_is_active(bool active)
  {
    internal_modal_message_loop_is_active_ = active;
  }
#endif

  void set_popup_rect(const gfx::Rect& rect) { popup_rect_ = rect; }

  static bool ImplementsThreadSafeReferenceCounting() { return true; }

protected:
  static void UIT_CloseView(gfx::NativeView view);
  static bool UIT_IsViewVisible(gfx::NativeView view);

  void UIT_CreateDevToolsClient(BrowserDevToolsAgent* agent);
  void UIT_DestroyDevToolsClient();

protected:
  CefWindowInfo window_info_;
  CefBrowserSettings settings_;
  // Handle of the browser window that opened this window.
  gfx::NativeView opener_;
  bool is_modal_;
  CefRefPtr<CefClient> client_;
  scoped_ptr<WebViewHost> webviewhost_;
  WebWidgetHost* popuphost_;
  gfx::Rect popup_rect_;
  scoped_ptr<BrowserWebViewDelegate> delegate_;
  scoped_ptr<BrowserWebViewDelegate> popup_delegate_;
  scoped_ptr<BrowserNavigationController> nav_controller_;
  scoped_ptr<PaintDelegate> paint_delegate_;

  scoped_ptr<BrowserDevToolsAgent> dev_tools_agent_;
  scoped_ptr<BrowserDevToolsClient> dev_tools_client_;

  CefString title_;

  double zoom_level_;
  bool can_go_back_;
  bool can_go_forward_;
  bool has_document_;

#if defined(OS_WIN)
  // Context object used to manage printing.
  printing::PrintingContext print_context_;

  // Used to re-enable the opener when a modal window gets closed.
  bool opener_was_disabled_by_modal_loop_;
  bool internal_modal_message_loop_is_active_;
#endif

  typedef std::map<CefString, CefFrame*> FrameMap;
  FrameMap frames_;
  CefFrame* main_frame_;

  typedef std::map<WebKit::WebFrame*, CefRefPtr<CefTrackManager> >
      FrameObjectMap;
  FrameObjectMap frame_objects_;

  // Unique browser ID assigned by the context.
  int unique_id_;
 
  // A temporary directory for FileSystem API.
  ScopedTempDir file_system_root_;

  IMPLEMENT_REFCOUNTING(CefBrowserImpl);
  IMPLEMENT_LOCKING(CefBrowserImpl);
};


// Implementation of CefFrame.
class CefFrameImpl : public CefFrame
{
public:
  CefFrameImpl(CefBrowserImpl* browser, const CefString& name);
  virtual ~CefFrameImpl();

  // CefFrame methods
  virtual void Undo() OVERRIDE { browser_->Undo(this); }
  virtual void Redo() OVERRIDE { browser_->Redo(this); }
  virtual void Cut() OVERRIDE { browser_->Cut(this); }
  virtual void Copy() OVERRIDE { browser_->Copy(this); }
  virtual void Paste() OVERRIDE { browser_->Paste(this); }
  virtual void Delete() OVERRIDE { browser_->Delete(this); }
  virtual void SelectAll() OVERRIDE { browser_->SelectAll(this); }
  virtual void Print() OVERRIDE { browser_->Print(this); }
  virtual void ViewSource() OVERRIDE { browser_->ViewSource(this); }
  virtual CefString GetSource() OVERRIDE { return browser_->GetSource(this); }
  virtual CefString GetText() OVERRIDE { return browser_->GetText(this); }
  virtual void LoadRequest(CefRefPtr<CefRequest> request) OVERRIDE
    { return browser_->LoadRequest(this, request); }
  virtual void LoadURL(const CefString& url) OVERRIDE
    { return browser_->LoadURL(this, url); }
  virtual void LoadString(const CefString& string,
                          const CefString& url) OVERRIDE
    { return browser_->LoadString(this, string, url); }
  virtual void LoadStream(CefRefPtr<CefStreamReader> stream,
                          const CefString& url) OVERRIDE
    { return browser_->LoadStream(this, stream, url); }
  virtual void ExecuteJavaScript(const CefString& jsCode, 
                                 const CefString& scriptUrl,
                                 int startLine) OVERRIDE
    { return browser_->ExecuteJavaScript(this, jsCode, scriptUrl, startLine); }
  virtual bool IsMain() OVERRIDE { return name_.empty(); }
  virtual bool IsFocused() OVERRIDE;
  virtual CefString GetName() OVERRIDE { return name_; }
  virtual CefString GetURL() OVERRIDE { return browser_->GetURL(this); }
  virtual CefRefPtr<CefBrowser> GetBrowser() OVERRIDE { return browser_.get(); }
  virtual void VisitDOM(CefRefPtr<CefDOMVisitor> visitor) OVERRIDE;

private:
  CefRefPtr<CefBrowserImpl> browser_;
  CefString name_;

  IMPLEMENT_REFCOUNTING(CefFrameImpl);
};

#endif // _BROWSER_IMPL_H
