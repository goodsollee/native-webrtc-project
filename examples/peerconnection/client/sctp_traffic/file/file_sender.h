#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <utility>
#include <vector>

class Conductor;  // Forward declaration.

namespace sctp::file {

// Sends dummy file data over SCTP according to a traffic profile.
class Sender {
 public:
  // Periodic pattern constructor.
  Sender(int kind, int file_size, int periodicity_ms);
  // Custom trace pattern constructor.
  Sender(int kind, const std::string& trace_path);
  ~Sender();

  void Start(Conductor& c);
  void Stop();

 private:
  enum class Mode { kPeriodic, kCustom };
  void RunPeriodic();
  void RunCustom();
  void LoadTrace(const std::string& path);

  Conductor* conductor_ = nullptr;
  int kind_;
  int file_size_ = 0;
  int periodicity_ms_ = 0;
  std::vector<std::pair<int, int>> custom_events_;  // size, delay_ms
  std::thread worker_;
  std::atomic<bool> running_{false};
  Mode mode_;
};

}  // namespace sctp::file

