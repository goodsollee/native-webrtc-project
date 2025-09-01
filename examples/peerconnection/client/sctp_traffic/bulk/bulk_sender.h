#pragma once
#include <vector>
#include "sctp_traffic/traffic.h"
#include "rtc_base/task_utils/repeating_task.h"
#include "api/task_queue/task_queue_base.h"
#include "api/task_queue/task_queue_factory.h"
#include "api/task_queue/default_task_queue_factory.h"

class Conductor;

namespace sctp::bulk {

struct Config {
  double target_mbps = 500.0;
  size_t chunk_bytes = 16 * 1024;
  uint64_t buffered_cap = 8 * 1024 * 1024;
  int pump_interval_ms = 10;
};

class Sender final : public sctp::Sender {
public:
  explicit Sender(Config cfg = {});
  ~Sender() override;

  void Start(Conductor& c) override;
  void Stop() override;

private:
  void PumpOnce(int64_t now_ms);

  Conductor* conductor_ = nullptr;
  Config cfg_;
  webrtc::RepeatingTaskHandle task_;

  std::vector<uint8_t> payload_;
  double target_bps_ = 0.0;
  double credit_bytes_ = 0.0;
  int64_t last_ms_ = 0;
};

} // namespace sctp::bulk
