#include "examples/peerconnection/client/traffic_profile.h"

#include <fstream>
#include <sstream>

std::vector<TrafficProfile> LoadProfiles(const std::string& path) {
  std::vector<TrafficProfile> profiles;
  std::ifstream file(path);
  if (!file.is_open()) {
    return profiles;
  }

  std::string line;
  bool first_line = true;
  while (std::getline(file, line)) {
    if (line.empty()) {
      continue;
    }
    // Skip header if present.
    if (first_line && line.find("Traffic") != std::string::npos) {
      first_line = false;
      continue;
    }
    first_line = false;
    std::istringstream ss(line);
    std::string item;
    TrafficProfile profile;

    std::getline(ss, profile.traffic_name, ',');
    std::getline(ss, profile.protocol, ',');
    std::getline(ss, profile.pattern, ',');

    std::getline(ss, item, ',');
    if (!item.empty()) profile.file_size = std::stoi(item);

    std::getline(ss, item, ',');
    if (!item.empty()) profile.periodicity = std::stoi(item);

    std::getline(ss, profile.custom_trace, ',');

    std::getline(ss, item, ',');
    if (!item.empty()) profile.max_bitrate = std::stoi(item);

    std::getline(ss, item, ',');
    if (!item.empty()) profile.frame_rate = std::stoi(item);

    std::getline(ss, profile.video_file_name, ',');

    profiles.push_back(std::move(profile));
  }
  return profiles;
}
