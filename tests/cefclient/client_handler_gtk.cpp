#include "include/cef.h"
#include "include/cef_wrapper.h"
#include "cefclient.h"
#include "client_handler.h"
#include "resource_util.h"
#include "string_util.h"
#include <iostream>
#include <gtk/gtk.h>

using namespace std;

// ClientHandler::ClientLifeSpanHandler implementation
bool ClientHandler::OnBeforePopup(CefRefPtr<CefBrowser> parentBrowser,
                                  const CefPopupFeatures& popupFeatures,
                                  CefWindowInfo& windowInfo,
                                  const CefString& url,
                                  CefRefPtr<CefClient>& client,
                                  CefBrowserSettings& settings)
{
  REQUIRE_UI_THREAD();

  return false;
}

bool ClientHandler::OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser,
                                     CefRefPtr<CefRequest> request,
                                     CefString& redirectUrl,
                                     CefRefPtr<CefStreamReader>& resourceStream,
                                     CefRefPtr<CefResponse> response,
                                     int loadFlags)
{
  REQUIRE_IO_THREAD();

  std::string url = request->GetURL();

  if(url == "http://tests/request") {
      // Show the request contents
      std::string dump;
      DumpRequestContents(request, dump);
      resourceStream = CefStreamReader::CreateForData(
          (void*)dump.c_str(), dump.size());
      response->SetMimeType("text/plain");
      response->SetStatus(200);
  } else if (strstr(url.c_str(), "/ps_logo2.png") != NULL) {
      // Any time we find "ps_logo2.png" in the URL substitute in our own image
      resourceStream = GetBinaryResourceReader("logo.png");
      response->SetMimeType("image/png");
      response->SetStatus(200);
  } else if(url == "http://tests/localstorage") {
      // Show the localstorage contents
      resourceStream = GetBinaryResourceReader("localstorage.html");
      response->SetMimeType("text/html");
      response->SetStatus(200);
  } else if(url == "http://tests/xmlhttprequest") {
      // Show the xmlhttprequest HTML contents
      resourceStream = GetBinaryResourceReader("xmlhttprequest.html");
      response->SetMimeType("text/html");
      response->SetStatus(200);
  } else if(url == "http://tests/domaccess") {
      // Show the domaccess HTML contents
      resourceStream = GetBinaryResourceReader("domaccess.html");
      response->SetMimeType("text/html");
      response->SetStatus(200);
  }

  return false;
} 

void ClientHandler::OnAddressChange(CefRefPtr<CefBrowser> browser,
                                    CefRefPtr<CefFrame> frame,
                                    const CefString& url)
{
  REQUIRE_UI_THREAD();
  GtkWidget* widget = GTK_WIDGET(browser->GetWindowHandle());
  gtk_widget_grab_focus(widget);
}

void ClientHandler::OnTitleChange(CefRefPtr<CefBrowser> browser,
                                  const CefString& title)
{
  REQUIRE_UI_THREAD();
}

void ClientHandler::SendNotification(NotificationType type)
{

}

void ClientHandler::SetLoading(bool isLoading)
{
  // TODO(port): Change button status.
}

void ClientHandler::SetNavState(bool canGoBack, bool canGoForward)
{
  // TODO(port): Change button status.
}

void ClientHandler::CloseMainWindow()
{
	// TODO(port): Close main window.
}
