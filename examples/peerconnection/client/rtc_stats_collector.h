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

class RTCStatsCollectorCallback : public webrtc::RTCStatsCollectorCallback {
public:
    RTCStatsCollectorCallback(std::ofstream& stats_file,
                             std::mutex& mutex); 

    ~RTCStatsCollectorCallback() override;

protected:
    void OnStatsDelivered(
        const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report) override;

private:
    void OnStatsDeliveredOnSignalingThread(
        rtc::scoped_refptr<const webrtc::RTCStatsReport> report);
    
    void ProcessInboundRTPStats(const webrtc::RTCStats& stats);

    std::ofstream& stats_file_;
    std::mutex& stats_mutex_;
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

    std::ofstream stats_file_;
    
    std::thread stats_thread_;          // Use std::thread instead of rtc::Thread
    std::mutex stats_mutex_;            // Mutex for thread safety
    std::condition_variable stop_cv_;   // To signal the thread to stop
    bool should_collect_ = false;       // Flag to control the thread loop
    rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection_;

    bool is_running_ = false;
    const int kStatsIntervalMs = 1000; // Collection interval in milliseconds
};
#endif  // RTC_STATS_COLLECTOR_H_