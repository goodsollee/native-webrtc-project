#include "sctp_traffic/bulk/bulk_receiver.h"
#include "examples/peerconnection/client/conductor.h"
#include "rtc_base/time_utils.h"
#include "rtc_base/logging.h"

using Kind = Conductor::TrafficKind;

namespace sctp::bulk {

Receiver::Receiver(int log_period_ms) : period_ms_(log_period_ms) {}
Receiver::~Receiver() { Detach(); }

void Receiver::Attach(Conductor& c) {
  conductor_ = &c;

  // 1) Register payload handler
  conductor_->RegisterPayloadHandler(Kind::kBulkTest,
      [this](absl::Span<const uint8_t> bytes) {
        rx_accum_ += bytes.size();
        rx_total_ += bytes.size();
      });

  // 2) Periodic log timer
  last_ms_ = rtc::TimeMillis();
  task_ = rtc::RepeatingTaskHandle::Start(
      *conductor_->signaling_thread(), [this]() {
        Tick();
        return webrtc::TimeDelta::Millis(period_ms_);
      });
}

void Receiver::Tick() {
  const int64_t now = rtc::TimeMillis();
  const double dt = (now - last_ms_) / 1000.0;
  last_ms_ = now;

  const uint64_t bytes = rx_accum_;
  rx_accum_ = 0;
  const double mbps = dt > 0 ? (bytes * 8.0) / (dt * 1e6) : 0.0;

  RTC_LOG(LS_INFO) << "[BULK][RX] " << mbps << " Mbps ("
                   << bytes << " B / " << dt << " s), total="
                   << rx_total_ << " B";
}

void Receiver::Detach() {
  if (task_.Running()) task_.Stop();
  conductor_ = nullptr;
}

} // namespace sctp::bulk
