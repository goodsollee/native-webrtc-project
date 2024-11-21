// websocket_client.cc
#include "examples/peerconnection/client/websocket_client.h"
#include "rtc_base/logging.h"

WebSocketClient::WebSocketClient()
    : context_(nullptr),
      websocket_(nullptr),
      is_connected_(false) {
  RTC_LOG(LS_INFO) << "WebSocketClient constructor";
  
  struct lws_context_creation_info info;
  memset(&info, 0, sizeof info);

  static struct lws_protocols protocols[] = {
      {
        "apprtc",               // name
        WebSocketClient::CallbackFunction,  // callback
        sizeof(WebSocketClient*),           // per_session_data_size - important!
        4096,                   // rx_buffer_size
        0,                      // id
        nullptr,               // user - will be set later
        0                      // tx_packet_size
      },
      { nullptr, nullptr, 0, 0, 0, nullptr, 0 }  // terminator
  };

  // Set the user pointer to this instance
  protocols[0].user = this;

  info.port = CONTEXT_PORT_NO_LISTEN;
  info.protocols = protocols;
  info.gid = -1;
  info.uid = -1;
  info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
  info.user = this;  // Store this pointer in context

  RTC_LOG(LS_INFO) << "Creating libwebsocket context...";
  context_ = lws_create_context(&info);
  if (!context_) {
    RTC_LOG(LS_ERROR) << "Failed to create libwebsocket context";
  }
}

WebSocketClient::~WebSocketClient() {
  Close();
  if (context_) {
    lws_context_destroy(context_);
  }
}

bool WebSocketClient::Connect(const std::string& url) {
  RTC_LOG(LS_INFO) << "Connecting to " << url;
  if (!context_) {
    RTC_LOG(LS_ERROR) << "No context available";
    return false;
  }

  if (!ParseURL(url)) {
    RTC_LOG(LS_ERROR) << "Failed to parse WebSocket URL";
    return false;
  }

  struct lws_client_connect_info info;
  memset(&info, 0, sizeof(info));

  info.context = context_;
  info.address = host_.c_str();
  info.port = port_;
  info.path = path_.c_str();
  info.host = host_.c_str();
  origin_ = "https://goodsol.overlinkapp.org";  // Set Origin header
  info.origin = origin_.c_str();
  info.protocol = "apprtc";

  if (protocol_ == "wss") {
    info.ssl_connection = LCCSCF_USE_SSL;
    info.ssl_connection |= LCCSCF_ALLOW_SELFSIGNED;
    info.ssl_connection |= LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
  } else {
    info.ssl_connection = 0;
  }

  info.userdata = this;

  RTC_LOG(LS_INFO) << "Creating libwebsocket connection...";
  websocket_ = lws_client_connect_via_info(&info);
  if (!websocket_) {
    RTC_LOG(LS_ERROR) << "Failed to connect websocket";
    return false;
  }
  RTC_LOG(LS_INFO) << "Websocket connected...";

  return true;
}
void WebSocketClient::Close() {
  if (websocket_) {
    lws_callback_on_writable(websocket_);
    websocket_ = nullptr;
  }
  is_connected_ = false;
}

bool WebSocketClient::SendMessage(const std::string& message) {
    if (!is_connected_) {
      RTC_LOG(LS_ERROR) << "Cannot send message - not connected";
      return false;
    }

    RTC_LOG(LS_INFO) << "Queueing message: " << message;
    send_queue_.push_back(message);
    
    if (websocket_) {
      lws_callback_on_writable(websocket_);
    }
    
    return true;
  }


int WebSocketClient::CallbackFunction(struct lws *wsi,
                                    enum lws_callback_reasons reason,
                                    void *user, void *in, size_t len) {
  RTC_LOG(LS_INFO) << "Callback reason: " << reason;
  
  // Get the WebSocketClient instance from user data
  WebSocketClient* client = static_cast<WebSocketClient*>(
      lws_context_user(lws_get_context(wsi)));
      
  if (!client) {
    RTC_LOG(LS_ERROR) << "No client in callback";
    return -1;
  }
  
  client->HandleCallback(wsi, reason, user, in, len);
  return 0;
}

// Add this helper method to make context access cleaner
void WebSocketClient::HandleCallback(struct lws *wsi,
                                   enum lws_callback_reasons reason,
                                   void *user, void *in, size_t len) {
  switch (reason) {
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
      RTC_LOG(LS_INFO) << "WebSocket connected";
      is_connected_ = true;
      if (connection_callback_) {
        connection_callback_(true);
      }
      break;

    case LWS_CALLBACK_CLIENT_RECEIVE:
      if (in && len > 0) {
        std::string message(static_cast<char*>(in), len);
        RTC_LOG(LS_INFO) << "Received message: " << message;
        if (message_callback_) {
          message_callback_(message);
        }
      }
      break;

    case LWS_CALLBACK_CLIENT_WRITEABLE:
      if (!send_queue_.empty()) {
        std::string& message = send_queue_.front();
        
        // Add LWS_PRE bytes for libwebsockets header
        std::vector<unsigned char> buf(LWS_PRE + message.length());
        memcpy(&buf[LWS_PRE], message.data(), message.length());

        int result = lws_write(wsi, &buf[LWS_PRE], message.length(), LWS_WRITE_TEXT);
        if (result < 0) {
          RTC_LOG(LS_ERROR) << "Error writing to websocket";
        } else {
          send_queue_.pop_front();
        }
        // If we have more messages, request another writable callback
        if (!send_queue_.empty()) {
            lws_callback_on_writable(wsi);
        }
      }
      break;

    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
      RTC_LOG(LS_ERROR) << "Connection error: " << (in ? static_cast<char*>(in) : "unknown");
      is_connected_ = false;
      if (connection_callback_) {
        connection_callback_(false);
      }
      break;

    case LWS_CALLBACK_CLIENT_CLOSED:
      RTC_LOG(LS_INFO) << "WebSocket closed";
      is_connected_ = false;
      if (connection_callback_) {
        connection_callback_(false);
      }
      break;

    case LWS_CALLBACK_WSI_DESTROY:
      websocket_ = nullptr;
      break;

    default:
      break;
  }
}

bool WebSocketClient::ParseURL(const std::string& url) {
  size_t pos = 0;
  size_t protocol_end = url.find("://", pos);
  if (protocol_end == std::string::npos) {
    RTC_LOG(LS_ERROR) << "Invalid URL: No protocol specified";
    return false;
  }
  protocol_ = url.substr(pos, protocol_end - pos);
  pos = protocol_end + 3;  // Skip "://"

  size_t path_start = url.find('/', pos);
  if (path_start == std::string::npos) {
    host_ = url.substr(pos);
    path_ = "/";
  } else {
    host_ = url.substr(pos, path_start - pos);
    path_ = url.substr(path_start);
  }

  // Check for port in host
  size_t port_pos = host_.find(':');
  if (port_pos != std::string::npos) {
    port_ = std::stoi(host_.substr(port_pos + 1));
    host_ = host_.substr(0, port_pos);
  } else {
    if (protocol_ == "ws") {
      port_ = 80;
    } else if (protocol_ == "wss") {
      port_ = 443;
    } else {
      RTC_LOG(LS_ERROR) << "Unknown protocol: " << protocol_;
      return false;
    }
  }

  return true;
}