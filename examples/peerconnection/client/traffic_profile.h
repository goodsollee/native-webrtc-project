#ifndef EXAMPLES_PEERCONNECTION_CLIENT_TRAFFIC_PROFILE_H_
#define EXAMPLES_PEERCONNECTION_CLIENT_TRAFFIC_PROFILE_H_

#include <string>
#include <vector>

struct TrafficProfile {
  std::string traffic_name;
  std::string protocol;
  std::string pattern;
  int file_size = 0;
  int periodicity = 0;
  std::string custom_trace;
  int max_bitrate = 0;
  int frame_rate = 0;
  std::string video_file_name;
};

// Loads traffic profiles from a CSV file at |path|.
// The CSV is expected to have columns matching the fields in TrafficProfile.
std::vector<TrafficProfile> LoadProfiles(const std::string& path);

#endif  // EXAMPLES_PEERCONNECTION_CLIENT_TRAFFIC_PROFILE_H_
