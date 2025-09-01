#pragma once
#include <cstdint>
#include "sctp_traffic/traffic.h"
#include "rtc_base/task_utils/repeating_task.h"

class Conductor;

namespace sctp::bulk {

class Receiver final : public sctp::Receiver {
public:
  explicit Receiver(int log_period_ms = 1000);
  ~Receiver() override;

  void Attach(Conductor& c) override;
  void Detach() override;

private:
  void Tick();

  Conductor* conductor_ = nullptr;
  int period_ms_;
  rtc::RepeatingTaskHandle task_;

  uint64_t rx_accum_ = 0;
  uint64_t rx_total_ = 0;
  int64_t  last_ms_  = 0;
};

} // namespace sctp::bulk
