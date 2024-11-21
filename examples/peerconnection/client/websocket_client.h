// websocket_client.h
#ifndef WEBSOCKET_CLIENT_H_
#define WEBSOCKET_CLIENT_H_

#include <string>
#include <functional>
#include <memory>
#include <libwebsockets.h>
#include <deque>

class WebSocketClient {
 public:
  using MessageCallback = std::function<void(const std::string&)>;
  using ConnectionCallback = std::function<void(bool)>;

  WebSocketClient();
  ~WebSocketClient();

  bool Connect(const std::string& url);
  void Close();
  bool SendMessage(const std::string& message);
  bool IsConnected() const { return is_connected_; }
  
  void SetMessageCallback(MessageCallback callback) {
    message_callback_ = std::move(callback);
  }
  
  void SetConnectionCallback(ConnectionCallback callback) {
    connection_callback_ = std::move(callback);
  }

  static int CallbackFunction(struct lws *wsi, 
                            enum lws_callback_reasons reason,
                            void *user, void *in, size_t len);

  bool ParseURL(const std::string& url);

 private:
  void HandleCallback(struct lws *wsi, 
                     enum lws_callback_reasons reason,
                     void *user, void *in, size_t len);

  struct lws_context *context_;
  struct lws *websocket_;
  bool is_connected_;
  MessageCallback message_callback_;
  ConnectionCallback connection_callback_;
  std::deque<std::string> send_queue_;  // Queue for outgoing messages

  std::string protocol_;
  std::string host_;
  int port_;
  std::string path_;
  std::string origin_;

};

#endif  // WEBSOCKET_CLIENT_H_