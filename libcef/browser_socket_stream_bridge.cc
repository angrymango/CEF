// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "browser_socket_stream_bridge.h"

#include "base/message_loop.h"
#include "base/ref_counted.h"
#include "googleurl/src/gurl.h"
#include "net/socket_stream/socket_stream.h"
#include "net/url_request/url_request_context.h"
#include "webkit/api/public/WebSocketStreamHandle.h"
#include "webkit/glue/websocketstreamhandle_bridge.h"
#include "webkit/glue/websocketstreamhandle_delegate.h"

using webkit_glue::WebSocketStreamHandleBridge;

static const int kNoSocketId = 0;

namespace {

MessageLoop* g_io_thread;
scoped_refptr<URLRequestContext> g_request_context;

class WebSocketStreamHandleBridgeImpl
    : public base::RefCountedThreadSafe<WebSocketStreamHandleBridgeImpl>,
      public WebSocketStreamHandleBridge,
      public net::SocketStream::Delegate {
 public:
  WebSocketStreamHandleBridgeImpl(
      WebKit::WebSocketStreamHandle* handle,
      webkit_glue::WebSocketStreamHandleDelegate* delegate);
  virtual ~WebSocketStreamHandleBridgeImpl();

  // WebSocketStreamHandleBridge methods.
  virtual void Connect(const GURL& url);
  virtual bool Send(const std::vector<char>& data);
  virtual void Close();

  // net::SocketStream::Delegate methods.
  virtual void OnConnected(net::SocketStream* req,
                           int max_pending_send_allowed);
  virtual void OnSentData(net::SocketStream* req,
                          int amount_sent);
  virtual void OnReceivedData(net::SocketStream* req,
                              const char* data, int len);
  virtual void OnClose(net::SocketStream* req);


 private:
  // Runs on |g_io_thread|;
  void DoConnect(const GURL& url);
  void DoSend(std::vector<char>* data);
  void DoClose();

  // Runs on |message_loop_|;
  void DoOnConnected(int max_amount_send_allowed);
  void DoOnSentData(int amount_sent);
  void DoOnReceivedData(std::vector<char>* data);
  void DoOnClose(webkit_glue::WebSocketStreamHandleDelegate* delegate);

  int socket_id_;
  MessageLoop* message_loop_;
  WebKit::WebSocketStreamHandle* handle_;
  webkit_glue::WebSocketStreamHandleDelegate* delegate_;

  scoped_refptr<net::SocketStream> socket_;

  DISALLOW_COPY_AND_ASSIGN(WebSocketStreamHandleBridgeImpl);
};

WebSocketStreamHandleBridgeImpl::WebSocketStreamHandleBridgeImpl(
    WebKit::WebSocketStreamHandle* handle,
    webkit_glue::WebSocketStreamHandleDelegate* delegate)
    : socket_id_(kNoSocketId),
      message_loop_(MessageLoop::current()),
      handle_(handle),
      delegate_(delegate) {
}

WebSocketStreamHandleBridgeImpl::~WebSocketStreamHandleBridgeImpl() {
  CHECK(socket_id_ == kNoSocketId);
}

void WebSocketStreamHandleBridgeImpl::Connect(const GURL& url) {
  CHECK(g_io_thread);
  AddRef();  // Released in DoOnClose().
  g_io_thread->PostTask(
      FROM_HERE,
      NewRunnableMethod(this, &WebSocketStreamHandleBridgeImpl::DoConnect,
                        url));
  if (delegate_)
    delegate_->WillOpenStream(handle_, url);
}

bool WebSocketStreamHandleBridgeImpl::Send(
    const std::vector<char>& data) {
  CHECK(g_io_thread);
  g_io_thread->PostTask(
      FROM_HERE,
      NewRunnableMethod(this, &WebSocketStreamHandleBridgeImpl::DoSend,
                        new std::vector<char>(data)));
  return true;
}

void WebSocketStreamHandleBridgeImpl::Close() {
  CHECK(g_io_thread);
  g_io_thread->PostTask(
      FROM_HERE,
      NewRunnableMethod(this, &WebSocketStreamHandleBridgeImpl::DoClose));
}

void WebSocketStreamHandleBridgeImpl::OnConnected(
    net::SocketStream* socket, int max_pending_send_allowed) {
  message_loop_->PostTask(
      FROM_HERE,
      NewRunnableMethod(this, &WebSocketStreamHandleBridgeImpl::DoOnConnected,
                        max_pending_send_allowed));
}

void WebSocketStreamHandleBridgeImpl::OnSentData(
    net::SocketStream* socket, int amount_sent) {
  message_loop_->PostTask(
      FROM_HERE,
      NewRunnableMethod(this, &WebSocketStreamHandleBridgeImpl::DoOnSentData,
                        amount_sent));
}

void WebSocketStreamHandleBridgeImpl::OnReceivedData(
    net::SocketStream* socket, const char* data, int len) {
  message_loop_->PostTask(
      FROM_HERE,
      NewRunnableMethod(this,
                        &WebSocketStreamHandleBridgeImpl::DoOnReceivedData,
                        new std::vector<char>(data, data + len)));
}

void WebSocketStreamHandleBridgeImpl::OnClose(net::SocketStream* socket) {
  webkit_glue::WebSocketStreamHandleDelegate* delegate = delegate_;
  delegate_ = NULL;
  socket_ = 0;
  socket_id_ = kNoSocketId;
  message_loop_->PostTask(
      FROM_HERE,
      NewRunnableMethod(this, &WebSocketStreamHandleBridgeImpl::DoOnClose,
                        delegate));
}

void WebSocketStreamHandleBridgeImpl::DoConnect(const GURL& url) {
  CHECK(MessageLoop::current() == g_io_thread);
  socket_ = new net::SocketStream(url, this);
  socket_->set_context(g_request_context);
  socket_->Connect();
}

void WebSocketStreamHandleBridgeImpl::DoSend(std::vector<char>* data) {
  CHECK(MessageLoop::current() == g_io_thread);
  scoped_ptr<std::vector<char> > scoped_data(data);
  if (!socket_)
    return;
  if (!socket_->SendData(&(data->at(0)), data->size()))
    socket_->Close();
}

void WebSocketStreamHandleBridgeImpl::DoClose() {
  CHECK(MessageLoop::current() == g_io_thread);
  if (!socket_)
    return;
  socket_->Close();
}

void WebSocketStreamHandleBridgeImpl::DoOnConnected(
    int max_pending_send_allowed) {
  CHECK(MessageLoop::current() == message_loop_);
  if (delegate_)
    delegate_->DidOpenStream(handle_, max_pending_send_allowed);
}

void WebSocketStreamHandleBridgeImpl::DoOnSentData(int amount_sent) {
  CHECK(MessageLoop::current() == message_loop_);
  if (delegate_)
    delegate_->DidSendData(handle_, amount_sent);
}

void WebSocketStreamHandleBridgeImpl::DoOnReceivedData(
    std::vector<char>* data) {
  CHECK(MessageLoop::current() == message_loop_);
  scoped_ptr<std::vector<char> > scoped_data(data);
  if (delegate_)
    delegate_->DidReceiveData(handle_, &(data->at(0)), data->size());
}

void WebSocketStreamHandleBridgeImpl::DoOnClose(
    webkit_glue::WebSocketStreamHandleDelegate* delegate) {
  CHECK(MessageLoop::current() == message_loop_);
  CHECK(!socket_);
  if (delegate)
    delegate->DidClose(handle_);
  Release();
}

}  // namespace

/* static */
void BrowserSocketStreamBridge::InitializeOnIOThread(
    URLRequestContext* request_context) {
  g_io_thread = MessageLoop::current();
  g_request_context = request_context;
}

void BrowserSocketStreamBridge::Cleanup() {
  g_io_thread = NULL;
  g_request_context = NULL;
}

namespace webkit_glue {

/* static */
WebSocketStreamHandleBridge* WebSocketStreamHandleBridge::Create(
    WebKit::WebSocketStreamHandle* handle,
    WebSocketStreamHandleDelegate* delegate) {
  return new WebSocketStreamHandleBridgeImpl(handle, delegate);
}

}  // namespace webkit_glue
