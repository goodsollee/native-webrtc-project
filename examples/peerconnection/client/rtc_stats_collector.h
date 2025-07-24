#ifndef RTC_STATS_COLLECTOR_H_
#define RTC_STATS_COLLECTOR_H_

#include <fstream>
#include <mutex>
#include <string>
#include <vector>
#include "api/peer_connection_interface.h"
#include "api/stats/rtc_stats.h"
#include "api/stats/rtc_stats_collector_callback.h"
#include "rtc_base/thread.h"
#include <thread>
#include <condition_variable>


// Add persistent stats structure
struct PersistentStats {
    int64_t frame_timing_count_ = 0;
    int64_t last_render_time_ms_ = -1;
    int64_t last_timestamp_ = -1;


    int64_t acc_frames_decoded_ = 0;
    int64_t acc_frames_dropped_ = 0;
    int64_t acc_frames_received_ = 0;
    double acc_framerate_ = 0.0;
    double acc_jitter_buffer_delay_ = 0.0;
    double acc_total_decode_time_ = 0.0;
    int acc_count_ = 0;  // Count for averaging
    int64_t last_average_time_ms_ = 0;  // Last time we wrote averages

    // For overall average bitrate
    int64_t total_bytes_received_ = 0;
    int64_t first_stats_time_ms_ = -1;  // Time of first stats collection
    double  acc_min_playout_delay_   = 0.0;   // **NEW – ms**
    
    // For current period average bitrate
    int64_t period_start_bytes_ = 0;
    int64_t period_start_time_ms_ = 0;

    int64_t last_remote_bytes_sent_      = 0;   // 최신 remote-outbound bytesSent
    int64_t first_remote_stats_time_ms_  = -1;  // 최초 도착 시각
    int64_t period_remote_start_bytes_   = 0;   // 직전 구간 시작 값
};

class RTCStatsCollectorCallback : public webrtc::RTCStatsCollectorCallback {
public:
    RTCStatsCollectorCallback(
        std::ofstream& per_frame_stats_file,
        std::ofstream& average_stats_file,
        std::mutex& stats_mutex,
        PersistentStats& persistent_stats);  // Add persistent stats
    ~RTCStatsCollectorCallback();


protected:
    void OnStatsDelivered(
        const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report) override;

private:
    void ProcessRemoteOutboundRTPStats(const webrtc::RTCStats& stats);
    
    void OnStatsDeliveredOnSignalingThread(
        rtc::scoped_refptr<const webrtc::RTCStatsReport> report);
    
    void ProcessInboundRTPStats(const webrtc::RTCStats& stats);

    std::ofstream& per_frame_stats_file_;
    std::ofstream& average_stats_file_;
    std::mutex& stats_mutex_;
    PersistentStats& persistent_stats_;  // Reference to persistent stats

    //const int kFrameTimingLogCount = 5; // 60 frames per second
};

class RTCStatsCollector {
public:
    RTCStatsCollector();
    ~RTCStatsCollector();

    bool Start(const std::string& filename,
               rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection);
    void Stop();

    bool IsRunning () { return is_running_;}

private:
    void CollectStats();
    void ThreadLoop();

    bool OpenStatsFile(const std::string& filename);
    void CloseStatsFile();

    std::ofstream per_frame_stats_file_;
    std::ofstream average_stats_file_;

    std::thread stats_thread_;          // Use std::thread instead of rtc::Thread
    std::mutex stats_mutex_;            // Mutex for thread safety
    std::condition_variable stop_cv_;   // To signal the thread to stop
    bool should_collect_ = false;       // Flag to control the thread loop
    rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection_;

    bool is_running_ = false;
    const int kStatsIntervalMs = 200; // Collection interval in milliseconds

    PersistentStats persistent_stats_;
};
#endif  // RTC_STATS_COLLECTOR_H_