#include "examples/peerconnection/client/sctp_traffic/file/file_sender.h"

#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>

#include "absl/types/span.h"
#include "examples/peerconnection/client/conductor.h"

namespace sctp::file {

Sender::Sender(int kind, int file_size, int periodicity_ms)
    : kind_(kind), file_size_(file_size), periodicity_ms_(periodicity_ms),
      mode_(Mode::kPeriodic) {}

Sender::Sender(int kind, const std::string& trace_path)
    : kind_(kind), mode_(Mode::kCustom) {
  LoadTrace(trace_path);
}

Sender::~Sender() { Stop(); }

void Sender::Start(Conductor& c) {
  conductor_ = &c;
  running_.store(true);
  if (mode_ == Mode::kPeriodic) {
    worker_ = std::thread([this]() { RunPeriodic(); });
  } else {
    worker_ = std::thread([this]() { RunCustom(); });
  }
}

void Sender::Stop() {
  running_.store(false);
  if (worker_.joinable())
    worker_.join();
}

void Sender::RunPeriodic() {
  std::vector<uint8_t> payload(file_size_, 0);
  while (running_.load()) {
    if (!conductor_->IsFlowOpen(
            static_cast<Conductor::TrafficKind>(kind_))) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
    conductor_->SendPayload(
        static_cast<Conductor::TrafficKind>(kind_),
        absl::Span<const uint8_t>(payload));
    std::this_thread::sleep_for(
        std::chrono::milliseconds(periodicity_ms_));
  }
}

void Sender::LoadTrace(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open())
    return;
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty())
      continue;
    std::istringstream ss(line);
    std::string size_s;
    std::string delay_s;
    if (!std::getline(ss, size_s, ','))
      continue;
    if (!std::getline(ss, delay_s, ','))
      continue;
    int size = std::stoi(size_s);
    int delay = std::stoi(delay_s);
    custom_events_.emplace_back(size, delay);
  }
}

void Sender::RunCustom() {
  for (const auto& ev : custom_events_) {
    if (!running_.load())
      break;
    while (running_.load() &&
           !conductor_->IsFlowOpen(
               static_cast<Conductor::TrafficKind>(kind_))) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!running_.load())
      break;
    std::vector<uint8_t> payload(ev.first, 0);
    conductor_->SendPayload(
        static_cast<Conductor::TrafficKind>(kind_),
        absl::Span<const uint8_t>(payload));
    std::this_thread::sleep_for(std::chrono::milliseconds(ev.second));
  }
}

}  // namespace sctp::file

