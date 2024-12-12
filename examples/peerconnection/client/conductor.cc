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
#include "api/video_codecs/builtin_video_decoder_factory.h"
#include "api/video_codecs/builtin_video_encoder_factory.h"
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
#include "test/testsupport/y4m_frame_generator.h"

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

// y4m reader
class Y4mVideoSource : public webrtc::VideoTrackSource {
 public:
  explicit Y4mVideoSource(std::unique_ptr<webrtc::test::FrameGeneratorCapturer> capturer)
      : VideoTrackSource(/*remote=*/false), capturer_(std::move(capturer)) {}  // Changed Y4mVideoTrackSource to VideoTrackSource
 protected:
  rtc::VideoSourceInterface<webrtc::VideoFrame>* source() override {
    return capturer_.get();
  }
 private:
  std::unique_ptr<webrtc::test::FrameGeneratorCapturer> capturer_;
};

bool Conductor::curl_initialized_ = false;

Conductor::Conductor(PeerConnectionClient* client, MainWindow* main_wnd)
    : peer_id_(-1), loopback_(false), client_(client), main_wnd_(main_wnd), curl_(nullptr) {
  client_->RegisterObserver(this);
  main_wnd->RegisterObserver(this);
}

Conductor::~Conductor() {
  RTC_DCHECK(!peer_connection_);
  CleanupCurl();
}

bool Conductor::connection_active() const {
  return peer_connection_ != nullptr;
}

void Conductor::Close() {
  client_->SignOut();
  DeletePeerConnection();
}

// Add static callback implementation:
size_t Conductor::WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    std::string* response = static_cast<std::string*>(userp);
    response->append(static_cast<char*>(contents), total_size);
    return total_size;
}

// In Conductor.cpp add the initialization code:
bool Conductor::InitializeCurl() {
    if (!curl_initialized_) {
        curl_global_init(CURL_GLOBAL_ALL);
        curl_initialized_ = true;
    }

    if (!curl_) {
        curl_ = curl_easy_init();
        if (!curl_) {
            RTC_LOG(LS_ERROR) << "Failed to initialize CURL";
            return false;
        }
    }
    
    return true;
}

// Modify CleanupCurl:
void Conductor::CleanupCurl() {
    if (curl_) {
        curl_easy_cleanup(curl_);
        curl_ = nullptr;
    }
    
    if (curl_initialized_) {
        curl_global_cleanup();
        curl_initialized_ = false;
    }
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
  deps.task_queue_factory = webrtc::CreateDefaultTaskQueueFactory();
  deps.audio_encoder_factory = webrtc::CreateBuiltinAudioEncoderFactory();
  deps.audio_decoder_factory = webrtc::CreateBuiltinAudioDecoderFactory();

  /*
  // Create video encoder factory with H264 first (highest priority)
  deps.video_encoder_factory =
      std::make_unique<webrtc::VideoEncoderFactoryTemplate<
          webrtc::OpenH264EncoderTemplateAdapter,  // H264 first
          webrtc::LibvpxVp8EncoderTemplateAdapter,
          webrtc::LibvpxVp9EncoderTemplateAdapter,
          webrtc::LibaomAv1EncoderTemplateAdapter>>();

  // Create video decoder factory with H264 first
  deps.video_decoder_factory =
      std::make_unique<webrtc::VideoDecoderFactoryTemplate<
          webrtc::OpenH264DecoderTemplateAdapter,  // H264 first
          webrtc::LibvpxVp8DecoderTemplateAdapter,
          webrtc::LibvpxVp9DecoderTemplateAdapter,
          webrtc::Dav1dDecoderTemplateAdapter>>();
  */
 // Create video encoder factory 
  auto video_encoder_factory = webrtc::CreateBuiltinVideoEncoderFactory();
  
  // Log supported codecs by the factory
  RTC_LOG(LS_INFO) << "Available video encoders:";
  std::vector<webrtc::SdpVideoFormat> supported_formats = 
      video_encoder_factory->GetSupportedFormats();
  for (const auto& format : supported_formats) {
    RTC_LOG(LS_INFO) << "  " << format.name;
    for (const auto& param : format.parameters) {
      RTC_LOG(LS_INFO) << "    " << param.first << ": " << param.second;
    }
  }

  // Don't create ADM - this will work make device even without audio devices
  deps.audio_mixer = nullptr;
  deps.audio_processing = nullptr;
  deps.adm = nullptr;
  deps.audio_processing_builder = nullptr;

  deps.video_encoder_factory = std::move(video_encoder_factory);
  deps.video_decoder_factory = webrtc::CreateBuiltinVideoDecoderFactory();

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
  options.disable_encryption = false;
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

  // 1. Limit ICE candidates
  config.candidate_network_policy = 
      webrtc::PeerConnectionInterface::CandidateNetworkPolicy::kCandidateNetworkPolicyLowCost;
  
  // 2. Set ICE transport type
  config.type = webrtc::PeerConnectionInterface::IceTransportsType::kAll;
  
  // 3. Prioritize UDP
  config.tcp_candidate_policy = 
      webrtc::PeerConnectionInterface::kTcpCandidatePolicyEnabled;


  config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
  webrtc::PeerConnectionInterface::IceServer server;
  server.uri = GetPeerConnectionString();
  config.servers.push_back(server);

  // Add Google STUN server as backup
  webrtc::PeerConnectionInterface::IceServer stun_server;
  stun_server.uri = "stun:stun.l.google.com:19302";
  config.servers.push_back(stun_server);

  // Basic configuration for WebRTC
  config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
  config.bundle_policy = webrtc::PeerConnectionInterface::kBundlePolicyMaxBundle;
  config.rtcp_mux_policy = webrtc::PeerConnectionInterface::kRtcpMuxPolicyRequire;

  // Setup proper ICE connection timeouts
  config.ice_connection_receiving_timeout = 5000;  // 5 seconds
  config.ice_backup_candidate_pair_ping_interval = 5000; // 5 seconds
  config.ice_check_min_interval = std::optional<int>(500); // 500ms minimum between checks
  config.continual_gathering_policy = 
      webrtc::PeerConnectionInterface::GATHER_CONTINUALLY;

  // Logging
  config.logging_folder = log_dir_; 

  // Generate and add certificates
  rtc::scoped_refptr<rtc::RTCCertificate> certificate = 
      rtc::RTCCertificateGenerator::GenerateCertificate(
          rtc::KeyParams(rtc::KT_DEFAULT), std::nullopt);
  if (certificate) {
    config.certificates.push_back(certificate);
  }

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
    RTC_LOG(LS_INFO) << "EnsureStreamingUI called, current UI: " 
                     << main_wnd_->current_ui();
    
    if (main_wnd_->IsWindow()) {
        if (main_wnd_->current_ui() != MainWindow::STREAMING) {
            RTC_LOG(LS_INFO) << "Switching to streaming UI";
            main_wnd_->SwitchToStreamingUI();
        }
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

  // Create candidate message
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
  std::string message = Json::writeString(factory, jmessage);
  
  if (is_initiator_) {
    // For initiator, send directly via HTTP
    SendMessage(message);
  } else {
    // For non-initiator, use websocket
    if (!ws_client_ || !ws_client_->IsConnected()) {
      RTC_LOG(LS_INFO) << "WebSocket not connected, queuing ICE candidate";
      pending_messages_.push_back(new std::string(message));  // Allocate new string
    } else {
      // Create WebSocket wrapper message
      Json::Value wrapped_message;
      wrapped_message["cmd"] = "send";
      wrapped_message["msg"] = message;
      std::string ws_message = Json::writeString(factory, wrapped_message);
      ws_client_->SendMessage(ws_message);
    }
  }
  
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

    // Add this call:
    EnsureStreamingUI();

    RTC_LOG (LS_INFO) << "Set remote description";
    peer_connected_ = true;


    // Now we can send any pending ICE candidates
    while (!pending_messages_.empty()) {
        ws_client_->SendMessage(*pending_messages_.front());
        pending_messages_.pop_front();
    }
    /*    
    while (!pending_messages_.empty()) {
        auto& message = pending_messages_.front();  // Get the front message
        if (message) {
            SendMessage(*message);  // Use SendMessage to send the message
        }
        pending_messages_.pop_front();  // Remove the sent message from the queue
    }
    */

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


    // Set high quality bitrate for 4K
    webrtc::BitrateSettings bitrate_settings;
    // For 4K video, use higher bitrates
    bitrate_settings.min_bitrate_bps =    200000;    // 200 kbps min
    bitrate_settings.start_bitrate_bps =  300000;  // 300 kbps start
    bitrate_settings.max_bitrate_bps =  50000000;    // 50 Mbps max
    peer_connection_->SetBitrate(bitrate_settings);

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
  if (room_id_.empty()) {
    room_id_ = GenerateRandomString(8);  // You can generate a random ID if needed
    RTC_LOG(LS_INFO) << "Generated room number is "<<room_id_;
  } 

  // Perform HTTP POST to /join/{room_id}
  std::string join_url = "https://" + server + "/join/" + room_id_;

  if (!InitializeCurl()) {
    RTC_LOG(LS_ERROR) << "Failed to initialize CURL";
    return;
  }

  CURLcode res;
  std::string read_buffer;

  if(curl_) {
    curl_easy_setopt(curl_, CURLOPT_URL, join_url.c_str());
    curl_easy_setopt(curl_, CURLOPT_POST, 1L);
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &read_buffer);

    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "User-Agent: peerconnection-client/1.0");
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);

    Json::Value join_payload;
    join_payload["room_id"] = room_id_;
    Json::StreamWriterBuilder writer;
    std::string payload = Json::writeString(writer, join_payload);
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, payload.length());

    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl_, CURLOPT_VERBOSE, 1L);

    RTC_LOG(LS_INFO) << "Server Response: " << read_buffer;

    res = curl_easy_perform(curl_);
    if(res != CURLE_OK) {
      RTC_LOG(LS_ERROR) << "curl_easy_perform() failed: " << curl_easy_strerror(res);
      curl_easy_cleanup(curl_);
      return;
    }
    curl_slist_free_all(headers);
    //curl_easy_cleanup(curl_);
  }

  /////
  // Parse server response
  Json::Value response;
  Json::CharReaderBuilder reader;
  std::istringstream response_stream(read_buffer);
  std::string parse_errors;

  if (!Json::parseFromStream(reader, response_stream, &response, &parse_errors)) {
    RTC_LOG(LS_ERROR) << "Failed to parse join response: " << parse_errors;
    return;
  }

  if (response["result"].asString() != "SUCCESS") {
    RTC_LOG(LS_ERROR) << "Join failed: " << response["result"].asString();
    return;
  }

  // Extract connection parameters
  Json::Value params = response["params"];
  is_initiator_ = params["is_initiator"].asString() == "true";
  std::string wss_url = params["wss_url"].asString();
  client_id_ = params["client_id"].asString();
  room_id_ = params["room_id"].asString();

  post_url_ = "https://" + server + "/message/" + room_id_ + "/" + client_id_;
  
  // Store initial messages if any
  if (params.isMember("messages") && params["messages"].isArray()) {
    initial_messages_ = params["messages"];
  }

  // Connect to WebSocket server
  ws_client_ = std::make_unique<WebSocketClient>();
  ws_client_->SetMessageCallback(
      std::bind(&Conductor::OnWebSocketMessage, this, std::placeholders::_1));
  ws_client_->SetConnectionCallback(
      std::bind(&Conductor::OnWebSocketConnection, this, std::placeholders::_1));

  RTC_LOG(LS_INFO) << "Connecting to WebSocket server: " << wss_url << " is_initiator: " << is_initiator_;
  ws_client_->Connect(wss_url);
}

void Conductor::OnWebSocketMessage(const std::string& message) {
  Json::CharReaderBuilder reader;
  Json::Value json_message;
  std::string parse_errors;
  std::istringstream message_stream(message);
  
  if (!Json::parseFromStream(reader, message_stream, &json_message, &parse_errors)) {
    RTC_LOG(LS_WARNING) << "Failed to parse WebSocket message: " << parse_errors;
    return;
  }

  std::string msg_data;
  if (json_message.isMember("msg")) {
    // Unwrap the message from the WebSocket envelope
    msg_data = json_message["msg"].asString();
  } else {
    msg_data = message;
  }

  RTC_LOG(LS_INFO) << "WebSocket msg received "<<msg_data;
  // Process the signaling message
  OnMessageFromPeer(-1, msg_data);
}


void Conductor::OnWebSocketConnection(bool connected) {
  if (connected) {
    RTC_LOG(LS_INFO) << "WebSocket connected, registering...";

    // Send registration message
    Json::Value reg_message;
    reg_message["cmd"] = "register";
    reg_message["roomid"] = room_id_;
    reg_message["clientid"] = client_id_;

    Json::StreamWriterBuilder writer;
    std::string message = Json::writeString(writer, reg_message);
    ws_client_->SendMessage(message);

    // Process any initial messages
    if (!initial_messages_.empty()) {
      for (const auto& msg : initial_messages_) {
        OnMessageFromPeer(-1, msg.asString());
      }
      initial_messages_.clear();
    }

    // If we're the initiator, create and send an offer
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

  // If we're in receiver-only mode, don't add any local tracks
  if (!is_sender_) {
    RTC_LOG(LS_INFO) << "Operating in receiver-only mode";
    main_wnd_->SwitchToStreamingUI();
    return;
  }

  bool use_camera = true;

  // Try Y4M first if path is provided
  if (!y4m_path_.empty()) {
    RTC_LOG(LS_INFO) << "Attempting to use Y4M file from path: " << y4m_path_;
    
    std::unique_ptr<webrtc::test::Y4mFrameGenerator> frame_generator(
        new webrtc::test::Y4mFrameGenerator(
            y4m_path_,
            webrtc::test::Y4mFrameGenerator::RepeatMode::kLoop
        ));

    if (frame_generator) {
      auto resolution = frame_generator->GetResolution();
      const int kTargetFps = frame_generator->fps().value_or(60);
      
      auto video_capturer = std::make_unique<webrtc::test::FrameGeneratorCapturer>(
          webrtc::Clock::GetRealTimeClock(),
          std::move(frame_generator),
          kTargetFps,
          *task_queue_factory_
      );

      if (video_capturer) {
        video_capturer->Start();
        rtc::scoped_refptr<Y4mVideoSource> video_source = 
            rtc::make_ref_counted<Y4mVideoSource>(std::move(video_capturer));
            
        rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track =
            peer_connection_factory_->CreateVideoTrack(kVideoLabel, video_source.get());

        main_wnd_->StartLocalRenderer(video_track.get());

        auto result_or_error = peer_connection_->AddTrack(video_track, {kStreamId});
        if (result_or_error.ok()) {
          use_camera = false;  // Successfully using Y4M
          RTC_LOG(LS_INFO) << "Successfully initialized Y4M video source";
        } else {
          RTC_LOG(LS_WARNING) << "Failed to add Y4M track to peer connection. Falling back to camera.";
        }
      } else {
        RTC_LOG(LS_WARNING) << "Failed to create Y4M video capturer. Falling back to camera.";
      }

      // Configure RTP encoding parameters for high quality
      auto senders = peer_connection_->GetSenders();
      rtc::scoped_refptr<webrtc::RtpSenderInterface> sender;
      for (const auto& s : senders) {
        if (s->track() && s->track()->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
          sender = s;
          break;
        }
      }

      if (sender) {
        webrtc::RtpParameters parameters = sender->GetParameters();
        
        // Log initial codecs
        RTC_LOG(LS_INFO) << "Available codecs before setting parameters:";
        for (const auto& codec : parameters.codecs) {
          RTC_LOG(LS_INFO) << "Codec: " << codec.name 
                          << " Payload: " << codec.payload_type;
        }

        // Configure H264 parameters
        for (webrtc::RtpCodecParameters& codec : parameters.codecs) {
          if (codec.name == "H264") {
            // High profile, Level 5.1
            codec.parameters["profile-level-id"] = "640033";
            codec.parameters["packetization-mode"] = "1";
            codec.parameters["level-asymmetry-allowed"] = "1";
            // For 4K support
            codec.parameters["max-mbps"] = "972000";
            codec.parameters["max-fs"] = std::to_string((resolution.width * resolution.height) / 256);

            RTC_LOG(LS_INFO) << "Configured H264 parameters:";
            for (const auto& param : codec.parameters) {
              RTC_LOG(LS_INFO) << "  " << param.first << ": " << param.second;
            }
          }
        }
        // Set high bitrate for encoding
        parameters.encodings.clear();
        webrtc::RtpEncodingParameters encoding;
        encoding.active = true;
        encoding.max_bitrate_bps = std::optional<int>(50000000);  // 40 Mbps for 4K
        encoding.max_framerate = std::optional<int>(60);          // Up to 60fps
        encoding.scale_resolution_down_by = std::optional<double>(1.0);  // No downscaling
        parameters.encodings.push_back(encoding);

        // Set the parameters
        webrtc::RTCError error = sender->SetParameters(parameters);
        if (!error.ok()) {
          RTC_LOG(LS_ERROR) << "Failed to set parameters: " << error.message();
        } else {
          RTC_LOG(LS_INFO) << "Successfully set encoding parameters";
        }

        // Verify encoder configuration
        auto final_params = sender->GetParameters();
        RTC_LOG(LS_INFO) << "Final encoder configuration:";
        for (const auto& codec : final_params.codecs) {
          RTC_LOG(LS_INFO) << "Using codec: " << codec.name;
          for (const auto& param : codec.parameters) {
            RTC_LOG(LS_INFO) << "  " << param.first << " = " << param.second;
          }
        }
      }
    } else {
      RTC_LOG(LS_WARNING) << "Failed to create Y4M frame generator. Falling back to camera.";
    }
  }

  // Fall back to camera if Y4M failed or wasn't specified
  if (use_camera) {
    RTC_LOG(LS_INFO) << "Using camera as video source";
    
    rtc::scoped_refptr<CapturerTrackSource> video_device =
        CapturerTrackSource::Create(*task_queue_factory_);
    
    if (video_device) {
      rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track_(
          peer_connection_factory_->CreateVideoTrack(video_device, kVideoLabel));
      main_wnd_->StartLocalRenderer(video_track_.get());

      auto result_or_error = peer_connection_->AddTrack(video_track_, {kStreamId});
      if (!result_or_error.ok()) {
        RTC_LOG(LS_ERROR) << "Failed to add video track to PeerConnection: "
                         << result_or_error.error().message();
      }
    } else {
      RTC_LOG(LS_ERROR) << "OpenVideoCaptureDevice failed";
    }
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

void Conductor::ServiceWebSocket() {
  if (ws_client_) {
    ws_client_->Service();
  }
}

void Conductor::SetEmulationMode(bool is_emulation, bool is_sender) {
  is_emulation_ = is_emulation;
  is_sender_ = is_sender;
}

void Conductor::SetNetInterface(std::string interface_name) {
  net_interface_ = interface_name;
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
        pending_messages_.push_back(std::move(msg));
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
  ServiceWebSocket();
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

  RTC_LOG(LS_INFO) << "OnSuccess msg "<< jmessage;

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
// Modify SendMessage in conductor.cc:
void Conductor::SendMessage(const std::string& json_object) {
  int a = 1;
  if (a) {
    // Use HTTP POST if initiator
    if (!InitializeCurl()) {
      RTC_LOG(LS_ERROR) << "Failed to initialize CURL for sending message";
      return;
    }

    response_buffer_.clear();
    struct curl_slist *headers = nullptr;
    bool request_success = false;

    do {
        headers = curl_slist_append(nullptr, "Content-Type: application/json");
        if (!headers) {
            RTC_LOG(LS_ERROR) << "Failed to create headers";
            break;
        }
        
        // Set all CURL options
        curl_easy_setopt(curl_, CURLOPT_URL, post_url_.c_str());
        curl_easy_setopt(curl_, CURLOPT_POST, 1L);
        curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, json_object.c_str());
        curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, json_object.length());
        curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response_buffer_);
        
        // Important SSL settings
        curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 0L);
        
        // Connection settings
        curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl_, CURLOPT_NOSIGNAL, 1L); // For multi-threaded apps

        RTC_LOG(LS_INFO) << "POST " << post_url_ << " " << json_object;

        CURLcode res = curl_easy_perform(curl_);
        if (res != CURLE_OK) {
            RTC_LOG(LS_ERROR) << "curl_easy_perform() failed: " << curl_easy_strerror(res);
            break;
        }

        long response_code;
        curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &response_code);
        if (response_code >= 200 && response_code < 300) {
            request_success = true;
        } else {
            RTC_LOG(LS_ERROR) << "HTTP error: " << response_code;
        }

    } while (false);

    if (headers) {
        curl_slist_free_all(headers);
    }

    if (!request_success) {
        RTC_LOG(LS_ERROR) << "Failed to send message";
    }

  } else {
    // Use WebSocket if not initiator
    if (!ws_client_ || !ws_client_->IsConnected()) {
      RTC_LOG(LS_ERROR) << "WebSocket not connected";
      return;
    }

    // Wrap the message in AppRTC format
    Json::Value wrapped_message;
    wrapped_message["cmd"] = "send";
    wrapped_message["msg"] = json_object;

    std::string message = rtc::JsonValueToString(wrapped_message);
    RTC_LOG(LS_INFO) << "Sending WebSocket message: " << message;
    ws_client_->SendMessage(message);
  }
}