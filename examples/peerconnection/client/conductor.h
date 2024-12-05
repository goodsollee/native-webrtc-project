/*
 *  Copyright 2012 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef EXAMPLES_PEERCONNECTION_CLIENT_CONDUCTOR_H_
#define EXAMPLES_PEERCONNECTION_CLIENT_CONDUCTOR_H_

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "api/data_channel_interface.h"
#include "api/jsep.h"
#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "api/rtc_error.h"
#include "api/rtp_receiver_interface.h"
#include "api/scoped_refptr.h"
#include "api/task_queue/task_queue_factory.h"
#include "examples/peerconnection/client/main_wnd.h"
#include "examples/peerconnection/client/peer_connection_client.h"
#include "rtc_base/thread.h"

#include "examples/peerconnection/client/websocket_client.h"
#include <curl/curl.h>
#include "json/value.h"

namespace webrtc {
class VideoCaptureModule;
}  // namespace webrtc

namespace cricket {
class VideoRenderer;
}  // namespace cricket

class Conductor : public webrtc::PeerConnectionObserver,
                  public webrtc::CreateSessionDescriptionObserver,
                  public PeerConnectionClientObserver,
                  public MainWndCallback {
 public:
  enum CallbackID {
    MEDIA_CHANNELS_INITIALIZED = 1,
    PEER_CONNECTION_CLOSED,
    SEND_MESSAGE_TO_PEER,
    NEW_TRACK_ADDED,
    TRACK_REMOVED,
  };

  Conductor(PeerConnectionClient* client, MainWindow* main_wnd);

  bool connection_active() const;

  void Close() override;
  
  void ServiceWebSocket();

  void SetRoomId(const std::string& room_id) { room_id_ = room_id; }

  void SetEmulationMode(bool is_emulation, bool is_sender);

  void SetNetInterface(std::string interface_name);

  void SetY4mPath(const std::string& path) { y4m_path_ = path; }

 protected:
  ~Conductor();
  bool InitializePeerConnection();
  bool ReinitializePeerConnectionForLoopback();
  bool CreatePeerConnection();
  void DeletePeerConnection();
  void EnsureStreamingUI();
  void AddTracks();

  //
  // PeerConnectionObserver implementation.
  //

  void OnSignalingChange(
      webrtc::PeerConnectionInterface::SignalingState new_state) override {}
  void OnAddTrack(
      rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver,
      const std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>>&
          streams) override;
  void OnRemoveTrack(
      rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override;
  void OnDataChannel(
      rtc::scoped_refptr<webrtc::DataChannelInterface> channel) override {}
  void OnRenegotiationNeeded() override {}
  void OnIceConnectionChange(
      webrtc::PeerConnectionInterface::IceConnectionState new_state) override {}
  void OnIceGatheringChange(
      webrtc::PeerConnectionInterface::IceGatheringState new_state) override {}
  void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override;
  void OnIceConnectionReceivingChange(bool receiving) override {}

  //
  // PeerConnectionClientObserver implementation.
  //

  void OnSignedIn() override;

  void OnDisconnected() override;

  void OnPeerConnected(int id, const std::string& name) override;

  void OnPeerDisconnected(int id) override;

  void OnMessageFromPeer(int peer_id, const std::string& message) override;

  void OnMessageSent(int err) override;

  void OnServerConnectionFailure() override;

  //
  // MainWndCallback implementation.
  //

  void StartLogin(const std::string& server, int port) override;

  void DisconnectFromServer() override;

  void ConnectToPeer(int peer_id) override;

  void DisconnectFromCurrentPeer() override;

  void UIThreadCallback(int msg_id, void* data) override;

  // CreateSessionDescriptionObserver implementation.
  void OnSuccess(webrtc::SessionDescriptionInterface* desc) override;
  void OnFailure(webrtc::RTCError error) override;

 protected:
  // Send a message to the remote peer.
  void SendMessage(const std::string& json_object);

  int peer_id_;
  bool loopback_;
  std::unique_ptr<rtc::Thread> signaling_thread_;
  webrtc::TaskQueueFactory* task_queue_factory_ = nullptr;
  rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection_;
  rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>
      peer_connection_factory_;
  PeerConnectionClient* client_;
  MainWindow* main_wnd_;
  std::deque<std::string*> pending_messages_;

 private:
  std::unique_ptr<WebSocketClient> ws_client_;

  void OnWebSocketMessage(const std::string& message);
  void OnWebSocketConnection(bool connected);
  
  Json::Value messages_;
  
  bool is_initiator_;
  bool peer_connected_ = false;
  
  std::string client_id_;
  std::string room_id_;
  std::string server_;
  Json::Value initial_messages_;

  std::string post_url_; // For HTTP POST when initiator

  static bool curl_initialized_;
  CURL* curl_ = nullptr;
  static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp);
  bool InitializeCurl();
  void CleanupCurl();
  std::string response_buffer_;

  std::string net_interface_;
  bool is_emulation_ = false;
  bool is_sender_ = true;
  std::string y4m_path_;
  
};

#endif  // EXAMPLES_PEERCONNECTION_CLIENT_CONDUCTOR_H_
