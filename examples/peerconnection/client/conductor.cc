/*
 *  Copyright 2012 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "examples/peerconnection/client/conductor.h"

#include <stddef.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/audio_options.h"
#include "api/create_peerconnection_factory.h"
#include "api/enable_media.h"
#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "api/rtc_error.h"
#include "api/rtp_receiver_interface.h"
#include "api/rtp_sender_interface.h"
#include "api/scoped_refptr.h"
#include "api/task_queue/default_task_queue_factory.h"
#include "api/task_queue/task_queue_factory.h"
#include "api/test/create_frame_generator.h"
#include "api/video/video_frame.h"
#include "api/video/video_source_interface.h"
#include "api/video_codecs/video_decoder_factory_template.h"
#include "api/video_codecs/video_decoder_factory_template_dav1d_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp9_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_open_h264_adapter.h"
#include "api/video_codecs/video_encoder_factory_template.h"
#include "api/video_codecs/video_encoder_factory_template_libaom_av1_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp9_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_open_h264_adapter.h"
#include "examples/peerconnection/client/defaults.h"
#include "examples/peerconnection/client/main_wnd.h"
#include "examples/peerconnection/client/peer_connection_client.h"
#include "json/reader.h"
#include "json/value.h"
#include "json/writer.h"
#include "modules/video_capture/video_capture.h"
#include "modules/video_capture/video_capture_factory.h"
#include "pc/video_track_source.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/strings/json.h"
#include "system_wrappers/include/clock.h"
#include "test/frame_generator_capturer.h"
#include "test/platform_video_capturer.h"
#include "test/test_video_capturer.h"

#include <cstdlib>
#include <ctime>

namespace {
using webrtc::test::TestVideoCapturer;

// AppRTC

// Names used for a IceCandidate JSON object.
//const char kCandidateSdpMidName[] = "sdpMid";
//const char kCandidateSdpMlineIndexName[] = "sdpMLineIndex";
//const char kCandidateSdpName[] = "candidate";

// Names used for a SessionDescription JSON object.
const char kSessionDescriptionTypeName[] = "type";
const char kSessionDescriptionSdpName[] = "sdp";

class DummySetSessionDescriptionObserver
    : public webrtc::SetSessionDescriptionObserver {
 public:
  static rtc::scoped_refptr<DummySetSessionDescriptionObserver> Create() {
    return rtc::make_ref_counted<DummySetSessionDescriptionObserver>();
  }
  virtual void OnSuccess() { RTC_LOG(LS_INFO) << __FUNCTION__; }
  virtual void OnFailure(webrtc::RTCError error) {
    RTC_LOG(LS_INFO) << __FUNCTION__ << " " << ToString(error.type()) << ": "
                     << error.message();
  }
};

std::unique_ptr<TestVideoCapturer> CreateCapturer(
    webrtc::TaskQueueFactory& task_queue_factory) {
  const size_t kWidth = 640;
  const size_t kHeight = 480;
  const size_t kFps = 30;
  std::unique_ptr<webrtc::VideoCaptureModule::DeviceInfo> info(
      webrtc::VideoCaptureFactory::CreateDeviceInfo());
  if (!info) {
    return nullptr;
  }
  int num_devices = info->NumberOfDevices();
  for (int i = 0; i < num_devices; ++i) {
    std::unique_ptr<TestVideoCapturer> capturer =
        webrtc::test::CreateVideoCapturer(kWidth, kHeight, kFps, i);
    if (capturer) {
      return capturer;
    }
  }
  auto frame_generator = webrtc::test::CreateSquareFrameGenerator(
      kWidth, kHeight, std::nullopt, std::nullopt);
  return std::make_unique<webrtc::test::FrameGeneratorCapturer>(
      webrtc::Clock::GetRealTimeClock(), std::move(frame_generator), kFps,
      task_queue_factory);
}
class CapturerTrackSource : public webrtc::VideoTrackSource {
 public:
  static rtc::scoped_refptr<CapturerTrackSource> Create(
      webrtc::TaskQueueFactory& task_queue_factory) {
    std::unique_ptr<TestVideoCapturer> capturer =
        CreateCapturer(task_queue_factory);
    if (capturer) {
      capturer->Start();
      return rtc::make_ref_counted<CapturerTrackSource>(std::move(capturer));
    }
    return nullptr;
  }

 protected:
  explicit CapturerTrackSource(std::unique_ptr<TestVideoCapturer> capturer)
      : VideoTrackSource(/*remote=*/false), capturer_(std::move(capturer)) {}

 private:
  rtc::VideoSourceInterface<webrtc::VideoFrame>* source() override {
    return capturer_.get();
  }

  std::unique_ptr<TestVideoCapturer> capturer_;
};

}  // namespace

Conductor::Conductor(PeerConnectionClient* client, MainWindow* main_wnd)
    : peer_id_(-1), loopback_(false), client_(client), main_wnd_(main_wnd) {
  client_->RegisterObserver(this);
  main_wnd->RegisterObserver(this);
}

Conductor::~Conductor() {
  RTC_DCHECK(!peer_connection_);
}

bool Conductor::connection_active() const {
  return peer_connection_ != nullptr;
}

void Conductor::Close() {
  client_->SignOut();
  DeletePeerConnection();
}

bool Conductor::InitializePeerConnection() {
  RTC_DCHECK(!peer_connection_factory_);
  RTC_DCHECK(!peer_connection_);

  if (!signaling_thread_.get()) {
    signaling_thread_ = rtc::Thread::CreateWithSocketServer();
    signaling_thread_->Start();
  }

  webrtc::PeerConnectionFactoryDependencies deps;
  deps.signaling_thread = signaling_thread_.get();
  deps.task_queue_factory = webrtc::CreateDefaultTaskQueueFactory(),
  deps.audio_encoder_factory = webrtc::CreateBuiltinAudioEncoderFactory();
  deps.audio_decoder_factory = webrtc::CreateBuiltinAudioDecoderFactory();
  deps.video_encoder_factory =
      std::make_unique<webrtc::VideoEncoderFactoryTemplate<
          webrtc::LibvpxVp8EncoderTemplateAdapter,
          webrtc::LibvpxVp9EncoderTemplateAdapter,
          webrtc::OpenH264EncoderTemplateAdapter,
          webrtc::LibaomAv1EncoderTemplateAdapter>>();
  deps.video_decoder_factory =
      std::make_unique<webrtc::VideoDecoderFactoryTemplate<
          webrtc::LibvpxVp8DecoderTemplateAdapter,
          webrtc::LibvpxVp9DecoderTemplateAdapter,
          webrtc::OpenH264DecoderTemplateAdapter,
          webrtc::Dav1dDecoderTemplateAdapter>>();
  webrtc::EnableMedia(deps);
  task_queue_factory_ = deps.task_queue_factory.get();
  peer_connection_factory_ =
      webrtc::CreateModularPeerConnectionFactory(std::move(deps));

  if (!peer_connection_factory_) {
    main_wnd_->MessageBox("Error", "Failed to initialize PeerConnectionFactory",
                          true);
    DeletePeerConnection();
    return false;
  }

  if (!CreatePeerConnection()) {
    main_wnd_->MessageBox("Error", "CreatePeerConnection failed", true);
    DeletePeerConnection();
  }

  AddTracks();

  return peer_connection_ != nullptr;
}

bool Conductor::ReinitializePeerConnectionForLoopback() {
  loopback_ = true;
  std::vector<rtc::scoped_refptr<webrtc::RtpSenderInterface>> senders =
      peer_connection_->GetSenders();
  peer_connection_ = nullptr;
  // Loopback is only possible if encryption is disabled.
  webrtc::PeerConnectionFactoryInterface::Options options;
  options.disable_encryption = true;
  peer_connection_factory_->SetOptions(options);
  if (CreatePeerConnection()) {
    for (const auto& sender : senders) {
      peer_connection_->AddTrack(sender->track(), sender->stream_ids());
    }
    peer_connection_->CreateOffer(
        this, webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
  }
  options.disable_encryption = false;
  peer_connection_factory_->SetOptions(options);
  return peer_connection_ != nullptr;
}

bool Conductor::CreatePeerConnection() {
  RTC_DCHECK(peer_connection_factory_);
  RTC_DCHECK(!peer_connection_);

  webrtc::PeerConnectionInterface::RTCConfiguration config;
  config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
  webrtc::PeerConnectionInterface::IceServer server;
  server.uri = GetPeerConnectionString();
  config.servers.push_back(server);

  webrtc::PeerConnectionDependencies pc_dependencies(this);
  auto error_or_peer_connection =
      peer_connection_factory_->CreatePeerConnectionOrError(
          config, std::move(pc_dependencies));
  if (error_or_peer_connection.ok()) {
    peer_connection_ = std::move(error_or_peer_connection.value());
  }
  return peer_connection_ != nullptr;
}

void Conductor::DeletePeerConnection() {
  main_wnd_->StopLocalRenderer();
  main_wnd_->StopRemoteRenderer();
  peer_connection_ = nullptr;
  peer_connection_factory_ = nullptr;
  peer_id_ = -1;
  loopback_ = false;
}

void Conductor::EnsureStreamingUI() {
  RTC_DCHECK(peer_connection_);
  if (main_wnd_->IsWindow()) {
    if (main_wnd_->current_ui() != MainWindow::STREAMING)
      main_wnd_->SwitchToStreamingUI();
  }
}

//
// PeerConnectionObserver implementation.
//

void Conductor::OnAddTrack(
    rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
    const std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>>&
        streams) {
  RTC_LOG(LS_INFO) << __FUNCTION__ << " " << receiver->id();
  main_wnd_->QueueUIThreadCallback(NEW_TRACK_ADDED,
                                   receiver->track().release());
}

void Conductor::OnRemoveTrack(
    rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) {
  RTC_LOG(LS_INFO) << __FUNCTION__ << " " << receiver->id();
  main_wnd_->QueueUIThreadCallback(TRACK_REMOVED, receiver->track().release());
}

void Conductor::OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {
  RTC_LOG(LS_INFO) << __FUNCTION__ << " " << candidate->sdp_mline_index();
  if (loopback_) {
    if (!peer_connection_->AddIceCandidate(candidate)) {
      RTC_LOG(LS_WARNING) << "Failed to apply the received candidate";
    }
    return;
  }

  Json::Value jmessage;
  jmessage["type"] = "candidate";
  jmessage["label"] = candidate->sdp_mline_index();
  jmessage["id"] = candidate->sdp_mid();
  std::string sdp;
  if (!candidate->ToString(&sdp)) {
    RTC_LOG(LS_ERROR) << "Failed to serialize candidate";
    return;
  }
  jmessage["candidate"] = sdp;

  Json::StreamWriterBuilder factory;
  SendMessage(Json::writeString(factory, jmessage));
}

//
// PeerConnectionClientObserver implementation.
//

void Conductor::OnSignedIn() {
  RTC_LOG(LS_INFO) << __FUNCTION__;
  main_wnd_->SwitchToPeerList(client_->peers());
}

void Conductor::OnDisconnected() {
  RTC_LOG(LS_INFO) << __FUNCTION__;

  DeletePeerConnection();

  if (main_wnd_->IsWindow())
    main_wnd_->SwitchToConnectUI();
}

void Conductor::OnPeerConnected(int id, const std::string& name) {
  RTC_LOG(LS_INFO) << __FUNCTION__;
  // Refresh the list if we're showing it.
  if (main_wnd_->current_ui() == MainWindow::LIST_PEERS)
    main_wnd_->SwitchToPeerList(client_->peers());
}

void Conductor::OnPeerDisconnected(int id) {
  RTC_LOG(LS_INFO) << __FUNCTION__;
  if (id == peer_id_) {
    RTC_LOG(LS_INFO) << "Our peer disconnected";
    main_wnd_->QueueUIThreadCallback(PEER_CONNECTION_CLOSED, NULL);
  } else {
    // Refresh the list if we're showing it.
    if (main_wnd_->current_ui() == MainWindow::LIST_PEERS)
      main_wnd_->SwitchToPeerList(client_->peers());
  }
}

void Conductor::OnMessageFromPeer(int peer_id, const std::string& message) {
  RTC_DCHECK(!message.empty());

  // Initialize PeerConnection if necessary
  if (!peer_connection_) {
    if (!InitializePeerConnection()) {
      RTC_LOG(LS_ERROR) << "Failed to initialize PeerConnection";
      return;
    }
  }

  // Parse the incoming message
  Json::CharReaderBuilder reader_builder;
  std::unique_ptr<Json::CharReader> reader(reader_builder.newCharReader());
  Json::Value jmessage;
  std::string errors;

  if (!reader->parse(message.data(), message.data() + message.length(), &jmessage, &errors)) {
    RTC_LOG(LS_WARNING) << "Failed to parse incoming message: " << errors;
    return;
  }

  // Extract the message type
  std::string type;
  if (!rtc::GetStringFromJsonObject(jmessage, "type", &type)) {
    RTC_LOG(LS_WARNING) << "Message does not contain 'type'";
    return;
  }

  if (type == "bye") {
    RTC_LOG(LS_INFO) << "Received 'bye' message";
    DisconnectFromCurrentPeer();
    return;
  }

  if (type == "offer" || type == "answer") {
    // Handle session description
    std::string sdp;
    if (!rtc::GetStringFromJsonObject(jmessage, "sdp", &sdp)) {
      RTC_LOG(LS_WARNING) << "Session description is missing 'sdp'";
      return;
    }

    webrtc::SdpType sdp_type = (type == "offer") ? webrtc::SdpType::kOffer : webrtc::SdpType::kAnswer;
    webrtc::SdpParseError error;
    std::unique_ptr<webrtc::SessionDescriptionInterface> session_description =
        webrtc::CreateSessionDescription(sdp_type, sdp, &error);
    if (!session_description) {
      RTC_LOG(LS_WARNING) << "Failed to parse session description: " << error.description;
      return;
    }

    RTC_LOG(LS_INFO) << "Received session description: " << type;

    peer_connection_->SetRemoteDescription(
        DummySetSessionDescriptionObserver::Create().get(),
        session_description.release());

    if (sdp_type == webrtc::SdpType::kOffer) {
      peer_connection_->CreateAnswer(
          this, webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
    }
    return;
  }

  if (type == "candidate") {
    // Handle ICE candidate
    std::string candidate_str;
    if (!rtc::GetStringFromJsonObject(jmessage, "candidate", &candidate_str)) {
      RTC_LOG(LS_WARNING) << "ICE candidate is missing 'candidate'";
      return;
    }
    std::string sdp_mid;
    if (!rtc::GetStringFromJsonObject(jmessage, "id", &sdp_mid)) {
      RTC_LOG(LS_WARNING) << "ICE candidate is missing 'id'";
      return;
    }
    int sdp_mline_index = 0;
    if (!rtc::GetIntFromJsonObject(jmessage, "label", &sdp_mline_index)) {
      RTC_LOG(LS_WARNING) << "ICE candidate is missing 'label'";
      return;
    }

    webrtc::SdpParseError error;
    std::unique_ptr<webrtc::IceCandidateInterface> candidate(
     webrtc::CreateIceCandidate(sdp_mid, sdp_mline_index, candidate_str, &error));
    if (!candidate) {
      RTC_LOG(LS_WARNING) << "Failed to parse ICE candidate: " << error.description;
      return;
    }

    if (!peer_connection_->AddIceCandidate(candidate.get())) {
      RTC_LOG(LS_WARNING) << "Failed to add ICE candidate";
      return;
    }
    RTC_LOG(LS_INFO) << "Added ICE candidate";
    return;
  }

  RTC_LOG(LS_WARNING) << "Received unknown message type: " << type;
}

void Conductor::OnMessageSent(int err) {
  // Process the next pending message if any.
  main_wnd_->QueueUIThreadCallback(SEND_MESSAGE_TO_PEER, NULL);
}

void Conductor::OnServerConnectionFailure() {
  main_wnd_->MessageBox("Error", ("Failed to connect to " + server_).c_str(),
                        true);
}

//
// MainWndCallback implementation.
//

/*
void Conductor::StartLogin(const std::string& server, int port) {
  if (client_->is_connected())
    return;
  server_ = server;
  client_->Connect(server, port, GetPeerName());
}
*/


std::string GenerateRandomString(size_t length) {
    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    static bool seeded = false;
    
    // Seed the random number generator once
    if (!seeded) {
        srand(static_cast<unsigned int>(time(nullptr)));
        seeded = true;
    }

    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result += alphanum[rand() % (sizeof(alphanum) - 1)];
    }
    return result;
}

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
  ((std::string*)userp)->append((char*)contents, size * nmemb);
  return size * nmemb;
}

// websocket version for apprtc
void Conductor::StartLogin(const std::string& server, int port) {
  if (ws_client_) {
    RTC_LOG(LS_WARNING) << "WebSocket client already exists";
    return;
  }

  // Generate or set room ID
  std::string room_id = GenerateRandomString(8);  // You can generate a random ID if needed

  RTC_LOG(LS_INFO) << "Room number is "<<room_id;

  // Perform HTTP POST to /join/{room_id}
  std::string join_url = "https://" + server + "/join/" + room_id;

  CURL* curl = curl_easy_init();
  CURLcode res;
  std::string read_buffer;

  if(curl) {
    curl_easy_setopt(curl, CURLOPT_URL, join_url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
    // Skip SSL verification for testing (not recommended for production)
    //curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    //curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "User-Agent: peerconnection-client/1.0");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    std::string payload = "{\"room_id\": \"" + room_id + "\"}";
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payload.length());
    

    RTC_LOG(LS_INFO) << "Server Response: " << read_buffer;

    res = curl_easy_perform(curl);
    if(res != CURLE_OK) {
      RTC_LOG(LS_ERROR) << "curl_easy_perform() failed: " << curl_easy_strerror(res);
      curl_easy_cleanup(curl);
      return;
    }
    curl_easy_cleanup(curl);
  } else {
    RTC_LOG(LS_ERROR) << "Failed to initialize curl";
    return;
  }

  // Parse the JSON response
  Json::Reader reader;
  Json::Value json_response;
  if (!reader.parse(read_buffer, json_response)) {
    RTC_LOG(LS_ERROR) << "Failed to parse join response: " << read_buffer;
    return;
  }

  if (json_response["result"] != "SUCCESS") {
    RTC_LOG(LS_ERROR) << "Join failed: " << json_response["result"].asString();
    return;
  }

  Json::Value params = json_response["params"];

  // Extract parameters
  is_initiator_ = params["is_initiator"].asString() == "true";
  std::string wss_url = params["wss_url"].asString();
  client_id_ = params["client_id"].asString();
  room_id_ = params["room_id"].asString();
  messages_ = params["messages"];

  // Now, connect to the WebSocket server at wss_url
  ws_client_ = std::make_unique<WebSocketClient>();
  ws_client_->SetMessageCallback(
      std::bind(&Conductor::OnWebSocketMessage, this, std::placeholders::_1));
  ws_client_->SetConnectionCallback(
      std::bind(&Conductor::OnWebSocketConnection, this, std::placeholders::_1));

  RTC_LOG(LS_INFO) << "Connecting to WebSocket server: " << wss_url;
  ws_client_->Connect(wss_url);
}

void Conductor::OnWebSocketMessage(const std::string& message) {
  Json::Reader reader;
  Json::Value json;
  if (!reader.parse(message, json)) {
    RTC_LOG(LS_WARNING) << "Failed to parse WebSocket message: " << message;
    return;
  }

  std::string type;
  std::string msg_data;

  if (json.isMember("msg")) {
    // Unwrap the message
    msg_data = json["msg"].asString();
    Json::Value inner_json;
    if (!reader.parse(msg_data, inner_json)) {
      RTC_LOG(LS_WARNING) << "Failed to parse inner message: " << msg_data;
      return;
    }
    if (inner_json.isMember("type")) {
      type = inner_json["type"].asString();
    }
  } else if (json.isMember("type")) {
    // Direct message
    type = json["type"].asString();
    msg_data = message;
  }

  if (!type.empty()) {
    OnMessageFromPeer(-1, msg_data);
  }
}


void Conductor::OnWebSocketConnection(bool connected) {
  if (connected) {
    RTC_LOG(LS_INFO) << "WebSocket connected, registering...";

    // Send registration message
    Json::Value reg_message;
    reg_message["cmd"] = "register";
    reg_message["roomid"] = room_id_;
    reg_message["clientid"] = client_id_;

    std::string message = rtc::JsonValueToString(reg_message);
    ws_client_->SendMessage(message);

    // Process any initial messages
    if (messages_.isArray()) {
      for (const auto& msg : messages_) {
        OnMessageFromPeer(-1, msg.asString());
      }
    }

    // If we are the initiator, create an offer
    if (is_initiator_) {
      if (InitializePeerConnection()) {
        peer_connection_->CreateOffer(
            this, webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
      } else {
        RTC_LOG(LS_ERROR) << "Failed to initialize PeerConnection";
      }
    }

  } else {
    RTC_LOG(LS_WARNING) << "WebSocket disconnected";
    main_wnd_->MessageBox("Error", "WebSocket connection failed", true);
  }
}

void Conductor::DisconnectFromServer() {
  if (ws_client_) {
    // Send bye message
    Json::Value bye_message;
    bye_message["type"] = "bye";
    SendMessage(rtc::JsonValueToString(bye_message));
    
    ws_client_->Close();
    ws_client_.reset();
  }
}

void Conductor::ConnectToPeer(int peer_id) {
  RTC_DCHECK(peer_id_ == -1);
  RTC_DCHECK(peer_id != -1);

  if (peer_connection_.get()) {
    main_wnd_->MessageBox(
        "Error", "We only support connecting to one peer at a time", true);
    return;
  }

  if (InitializePeerConnection()) {
    peer_id_ = peer_id;
    peer_connection_->CreateOffer(
        this, webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
  } else {
    main_wnd_->MessageBox("Error", "Failed to initialize PeerConnection", true);
  }
}

void Conductor::AddTracks() {
  if (!peer_connection_->GetSenders().empty()) {
    return;  // Already added tracks.
  }

  rtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track(
      peer_connection_factory_->CreateAudioTrack(
          kAudioLabel,
          peer_connection_factory_->CreateAudioSource(cricket::AudioOptions())
              .get()));
  auto result_or_error = peer_connection_->AddTrack(audio_track, {kStreamId});
  if (!result_or_error.ok()) {
    RTC_LOG(LS_ERROR) << "Failed to add audio track to PeerConnection: "
                      << result_or_error.error().message();
  }

  rtc::scoped_refptr<CapturerTrackSource> video_device =
      CapturerTrackSource::Create(*task_queue_factory_);
  if (video_device) {
    rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track_(
        peer_connection_factory_->CreateVideoTrack(video_device, kVideoLabel));
    main_wnd_->StartLocalRenderer(video_track_.get());

    result_or_error = peer_connection_->AddTrack(video_track_, {kStreamId});
    if (!result_or_error.ok()) {
      RTC_LOG(LS_ERROR) << "Failed to add video track to PeerConnection: "
                        << result_or_error.error().message();
    }
  } else {
    RTC_LOG(LS_ERROR) << "OpenVideoCaptureDevice failed";
  }

  main_wnd_->SwitchToStreamingUI();
}

void Conductor::DisconnectFromCurrentPeer() {
  RTC_LOG(LS_INFO) << __FUNCTION__;
  if (peer_connection_.get()) {
    client_->SendHangUp(peer_id_);
    DeletePeerConnection();
  }

  if (main_wnd_->IsWindow())
    main_wnd_->SwitchToPeerList(client_->peers());
}

void Conductor::UIThreadCallback(int msg_id, void* data) {
  switch (msg_id) {
    case PEER_CONNECTION_CLOSED:
      RTC_LOG(LS_INFO) << "PEER_CONNECTION_CLOSED";
      DeletePeerConnection();

      if (main_wnd_->IsWindow()) {
        if (client_->is_connected()) {
          main_wnd_->SwitchToPeerList(client_->peers());
        } else {
          main_wnd_->SwitchToConnectUI();
        }
      } else {
        DisconnectFromServer();
      }
      break;

    case SEND_MESSAGE_TO_PEER: {
      RTC_LOG(LS_INFO) << "SEND_MESSAGE_TO_PEER";
      std::string* msg = reinterpret_cast<std::string*>(data);
      if (msg) {
        // For convenience, we always run the message through the queue.
        // This way we can be sure that messages are sent to the server
        // in the same order they were signaled without much hassle.
        pending_messages_.push_back(msg);
      }

      if (!pending_messages_.empty() && !client_->IsSendingMessage()) {
        msg = pending_messages_.front();
        pending_messages_.pop_front();

        if (!client_->SendToPeer(peer_id_, *msg) && peer_id_ != -1) {
          RTC_LOG(LS_ERROR) << "SendToPeer failed";
          DisconnectFromServer();
        }
        delete msg;
      }

      if (!peer_connection_.get())
        peer_id_ = -1;

      break;
    }

    case NEW_TRACK_ADDED: {
      auto* track = reinterpret_cast<webrtc::MediaStreamTrackInterface*>(data);
      if (track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
        auto* video_track = static_cast<webrtc::VideoTrackInterface*>(track);
        main_wnd_->StartRemoteRenderer(video_track);
      }
      track->Release();
      break;
    }

    case TRACK_REMOVED: {
      // Remote peer stopped sending a track.
      auto* track = reinterpret_cast<webrtc::MediaStreamTrackInterface*>(data);
      track->Release();
      break;
    }

    default:
      RTC_DCHECK_NOTREACHED();
      break;
  }
}

void Conductor::OnSuccess(webrtc::SessionDescriptionInterface* desc) {
  peer_connection_->SetLocalDescription(
      DummySetSessionDescriptionObserver::Create().get(), desc);

  std::string sdp;
  desc->ToString(&sdp);

  // For loopback test. To save some connecting delay.
  if (loopback_) {
    // Replace message type from "offer" to "answer"
    std::unique_ptr<webrtc::SessionDescriptionInterface> session_description =
        webrtc::CreateSessionDescription(webrtc::SdpType::kAnswer, sdp);
    peer_connection_->SetRemoteDescription(
        DummySetSessionDescriptionObserver::Create().get(),
        session_description.release());
    return;
  }

  Json::Value jmessage;
  jmessage[kSessionDescriptionTypeName] =
      webrtc::SdpTypeToString(desc->GetType());
  jmessage[kSessionDescriptionSdpName] = sdp;

  Json::StreamWriterBuilder factory;
  SendMessage(Json::writeString(factory, jmessage));
}

void Conductor::OnFailure(webrtc::RTCError error) {
  RTC_LOG(LS_ERROR) << ToString(error.type()) << ": " << error.message();
}

/*
void Conductor::SendMessage(const std::string& json_object) {
  std::string* msg = new std::string(json_object);
  main_wnd_->QueueUIThreadCallback(SEND_MESSAGE_TO_PEER, msg);
}
*/
void Conductor::SendMessage(const std::string& json_object) {
  if (!ws_client_ || !ws_client_->IsConnected()) {
    RTC_LOG(LS_ERROR) << "WebSocket not connected";
    return;
  }

  // Wrap the message in AppRTC format
  Json::Value wrapped_message;
  wrapped_message["cmd"] = "send";
  wrapped_message["msg"] = json_object;

  std::string message = rtc::JsonValueToString(wrapped_message);
  RTC_LOG(LS_INFO) << "Sending message: " << message;
  ws_client_->SendMessage(message);
}
