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
      "apprtc",               // protocol name
      WebSocketClient::CallbackFunction,  // callback function
      sizeof(WebSocketClient*),           // per_session_data_size
      4096,                   // receive buffer size
    },
    { nullptr, nullptr, 0, 0 }  // terminator
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

const char* GetCallbackReasonName(enum lws_callback_reasons reason) {
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED: return "LWS_CALLBACK_ESTABLISHED";
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: return "LWS_CALLBACK_CLIENT_CONNECTION_ERROR";
        case LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH: return "LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH";
        case LWS_CALLBACK_CLIENT_ESTABLISHED: return "LWS_CALLBACK_CLIENT_ESTABLISHED";
        case LWS_CALLBACK_CLOSED: return "LWS_CALLBACK_CLOSED";
        case LWS_CALLBACK_CLOSED_CLIENT_HTTP: return "LWS_CALLBACK_CLOSED_CLIENT_HTTP";
        case LWS_CALLBACK_RECEIVE: return "LWS_CALLBACK_RECEIVE";
        case LWS_CALLBACK_CLIENT_RECEIVE: return "LWS_CALLBACK_CLIENT_RECEIVE";
        case LWS_CALLBACK_CLIENT_RECEIVE_PONG: return "LWS_CALLBACK_CLIENT_RECEIVE_PONG";
        case LWS_CALLBACK_CLIENT_WRITEABLE: return "LWS_CALLBACK_CLIENT_WRITEABLE";
        case LWS_CALLBACK_SERVER_WRITEABLE: return "LWS_CALLBACK_SERVER_WRITEABLE";
        case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER: return "LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER";
        case LWS_CALLBACK_WSI_DESTROY: return "LWS_CALLBACK_WSI_DESTROY";
        case LWS_CALLBACK_PROTOCOL_INIT: return "LWS_CALLBACK_PROTOCOL_INIT";
        case LWS_CALLBACK_PROTOCOL_DESTROY: return "LWS_CALLBACK_PROTOCOL_DESTROY";
        case LWS_CALLBACK_CLIENT_CLOSED: return "LWS_CALLBACK_CLIENT_CLOSED";
        case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ: return "LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ";
        case LWS_CALLBACK_RECEIVE_CLIENT_HTTP: return "LWS_CALLBACK_RECEIVE_CLIENT_HTTP";
        case LWS_CALLBACK_CLIENT_HTTP_WRITEABLE: return "LWS_CALLBACK_CLIENT_HTTP_WRITEABLE";
        case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS: return "LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS";
        case LWS_CALLBACK_CLIENT_CONFIRM_EXTENSION_SUPPORTED: return "LWS_CALLBACK_CLIENT_CONFIRM_EXTENSION_SUPPORTED";
        default: return "Unknown callback reason";
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
  info.host = info.address;
  info.origin = "https://goodsol.overlinkapp.org";
  info.protocol = "apprtc";
  info.local_protocol_name = "apprtc";
  
  if (protocol_ == "wss") {
    info.ssl_connection = LCCSCF_USE_SSL | 
                         LCCSCF_ALLOW_SELFSIGNED | 
                         LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
  }

  RTC_LOG(LS_INFO) << "Creating WebSocket connection...";
  RTC_LOG(LS_INFO) << "Address: " << info.address;
  RTC_LOG(LS_INFO) << "Port: " << info.port;
  RTC_LOG(LS_INFO) << "Path: " << info.path;
  
  websocket_ = lws_client_connect_via_info(&info);
  if (!websocket_) {
    RTC_LOG(LS_ERROR) << "Failed to connect websocket";
    return false;
  }

  // Service the event loop a few times to establish the connection
  for (int i = 0; i < 50 && !is_connected_; i++) {
    lws_service(context_, 50);
    rtc::Thread::Current()->SleepMs(100);
  }

  return is_connected_;
}

void WebSocketClient::Close() {
  if (websocket_) {
    lws_callback_on_writable(websocket_);
    websocket_ = nullptr;
  }
  is_connected_ = false;
}

bool WebSocketClient::SendMessage(const std::string& message) {
  RTC_LOG(LS_INFO) << "Queuing message for sending: " << message;
  
  if (!is_connected_) {
    RTC_LOG(LS_ERROR) << "Cannot send message - not connected";
    return false;
  }

  send_queue_.push_back(message);
  
  if (websocket_) {
    RTC_LOG(LS_INFO) << "Requesting writable callback";
    int result = lws_callback_on_writable(websocket_);
    RTC_LOG(LS_INFO) << "lws_callback_on_writable result: " << result;
    
    // Also trigger service to process the callback request
    if (context_) {
      lws_service(context_, 0);
    }
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
  
  RTC_LOG(LS_INFO) << "Handling callback reason: " << reason << " (" << GetCallbackReasonName(reason) << ")";
               
  switch (reason) {
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
      RTC_LOG(LS_INFO) << "WebSocket client connection established";
      is_connected_ = true;
      if (connection_callback_) {
        connection_callback_(true);
      }
      // Schedule a writable callback to send any queued messages
      if (!send_queue_.empty()) {
        lws_callback_on_writable(wsi);
      }
      break;

    case LWS_CALLBACK_CLIENT_RECEIVE: {
      if (!in || len == 0) break;

      // Copy the message data
      std::string payload(static_cast<const char*>(in), len);
      RTC_LOG(LS_INFO) << "WebSocket received raw data: " << payload;

      // Parse the received JSON
      Json::CharReaderBuilder reader;
      Json::Value json_message;
      std::string parse_errors;
      std::istringstream message_stream(payload);
      
      if (!Json::parseFromStream(reader, message_stream, &json_message, &parse_errors)) {
        RTC_LOG(LS_WARNING) << "Failed to parse WebSocket message: " << parse_errors;
        return;
      }

      // Check for error messages from server
      if (json_message.isMember("error")) {
        RTC_LOG(LS_WARNING) << "Server error: " << json_message["error"].asString();
        return;
      }

      // Only process messages that have valid content
      if (json_message.isMember("msg")) {
        std::string msg_data = json_message["msg"].asString();
        if (!msg_data.empty()) {
          if (message_callback_) {
            message_callback_(msg_data);
          }
        } else {
          RTC_LOG(LS_WARNING) << "Received empty message content";
        }
      }
      break;
    }

    case LWS_CALLBACK_CLIENT_WRITEABLE: {
      if (!send_queue_.empty()) {
        std::string& message = send_queue_.front();
        
        // Add LWS_PRE bytes for libwebsockets header
        std::vector<unsigned char> buf(LWS_PRE + message.length());
        memcpy(&buf[LWS_PRE], message.data(), message.length());

        RTC_LOG(LS_INFO) << "Writing message to WebSocket: " << message;
        
        int result = lws_write(wsi, &buf[LWS_PRE], message.length(), LWS_WRITE_TEXT);
        if (result < 0) {
          RTC_LOG(LS_ERROR) << "Error writing to websocket";
          break;
        } else if (result < (int)message.length()) {
          RTC_LOG(LS_ERROR) << "Partial write to websocket";
          break;
        }
        
        send_queue_.pop_front();
        
        // If we have more messages, request another writable callback
        if (!send_queue_.empty()) {
          lws_callback_on_writable(wsi);
        }
      }
      break;
    }

    case LWS_CALLBACK_CLIENT_CLOSED:
      RTC_LOG(LS_INFO) << "WebSocket connection closed";
      is_connected_ = false;
      websocket_ = nullptr;
      if (connection_callback_) {
        connection_callback_(false);
      }
      break;

    case LWS_CALLBACK_WSI_DESTROY:
      RTC_LOG(LS_INFO) << "WebSocket instance destroyed";
      websocket_ = nullptr;
      break;

    default:
      RTC_LOG(LS_INFO) << "Unhandled callback reason: " << reason;
      break;
  }
}

void WebSocketClient::Service() {
    if (context_) {
        lws_service(context_, 0);
        
        // Trigger next service immediately if connected
        if (is_connected_ && websocket_) {
            lws_callback_on_writable(websocket_);
        }
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