#include "examples/peerconnection/client/rtc_stats_collector.h"
#include "rtc_base/logging.h"
#include "rtc_base/time_utils.h"

RTCStatsCollectorCallback::RTCStatsCollectorCallback(
    std::ofstream& stats_file,
    std::mutex& mutex)
    : stats_file_(stats_file),
      stats_mutex_(mutex) {
    RTC_LOG(LS_INFO) << "RTCStatsCollectorCallback created.";
}

RTCStatsCollectorCallback::~RTCStatsCollectorCallback() {
    RTC_LOG(LS_INFO) << "RTCStatsCollectorCallback destroyed.";
}

void RTCStatsCollectorCallback::OnStatsDelivered(
    const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
    RTC_LOG(LS_INFO) << "OnStatsDelivered called. Posting task to signaling thread.";
    OnStatsDeliveredOnSignalingThread(report);
}

void RTCStatsCollectorCallback::ProcessInboundRTPStats(const webrtc::RTCStats& stats) {
    std::vector<webrtc::Attribute> attributes = stats.Attributes();
    auto find_attribute = [&attributes](const std::string& name) -> const webrtc::Attribute* {
        for (const auto& attribute : attributes) {
            if (attribute.name() == name) {
                RTC_LOG(LS_INFO) << "Found attribute " << name << " = " << attribute.ToString();
                return &attribute;
            }
        }
        RTC_LOG(LS_WARNING) << "Attribute not found: " << name;
        return nullptr;
    };

    // Helper function to get numeric values safely without exceptions
    auto get_numeric = [&find_attribute](const std::string& name, double default_value = 0) -> double {
        const auto* attr = find_attribute(name);
        if (!attr || attr->ToString() == "null") {
            return default_value;  // Return default if null
        }
        const std::string& value_str = attr->ToString();
        char* end;
        double value = std::strtod(value_str.c_str(), &end);
        if (end == value_str.c_str()) {
            RTC_LOG(LS_WARNING) << "Failed to convert value for " << name << ": " << value_str;
            return default_value;  // Return default on conversion failure
        }
        RTC_LOG(LS_INFO) << "Got numeric value for " << name << ": " << value;
        return value;
    };

    // Extract all stats with null handling
    int64_t frames_decoded = static_cast<int64_t>(get_numeric("framesDecoded"));
    int64_t frames_dropped = static_cast<int64_t>(get_numeric("framesDropped"));
    int64_t frames_received = static_cast<int64_t>(get_numeric("framesReceived"));
    double framerate = get_numeric("framesPerSecond");
    double jitter_buffer_delay = get_numeric("jitterBufferDelay") * 1000.0;
    int64_t width = static_cast<int64_t>(get_numeric("frameWidth"));
    int64_t height = static_cast<int64_t>(get_numeric("frameHeight"));
    double total_decode_time = get_numeric("totalDecodeTime") * 1000.0;
    int64_t packets_received = static_cast<int64_t>(get_numeric("packetsReceived"));

    // Handle decoder implementation
    const auto* decoder_impl_attr = find_attribute("decoderImplementation");
    std::string decoder_implementation =
        decoder_impl_attr ? decoder_impl_attr->ToString() : "unknown";
    if (decoder_implementation == "null") {
        decoder_implementation = "unknown";
    }

    // Write stats to file with mutex protection
    if (frames_decoded > 0) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        if (stats_file_.is_open()) {
            stats_file_ << rtc::TimeMillis() << ","
                    << frames_decoded << ","
                    << frames_dropped << ","
                    << frames_received << ","
                    << framerate << ","
                    << jitter_buffer_delay << ","
                    << width << ","
                    << height << ","
                    << total_decode_time << ","
                    << packets_received << ","
                    << decoder_implementation << "\n";
            stats_file_.flush();
        }
    }
}

void RTCStatsCollectorCallback::OnStatsDeliveredOnSignalingThread(
    rtc::scoped_refptr<const webrtc::RTCStatsReport> report) {
    RTC_LOG(LS_INFO) << "OnStatsDeliveredOnSignalingThread called.";

    if (!report) {
        RTC_LOG(LS_ERROR) << "Null stats report received";
        return;
    }

    for (const auto& stats : *report) {
        std::vector<webrtc::Attribute> attributes = stats.Attributes();
        auto find_attribute = [&attributes](const std::string& name) -> const webrtc::Attribute* {
            for (const auto& attribute : attributes) {
                if (attribute.name() == name) {
                    return &attribute;
                }
            }
            return nullptr;
        };

        const auto* kind = find_attribute("kind");
        if (!kind) {
            RTC_LOG(LS_INFO) << "Skipping stats " << stats.id() << " because 'kind' attribute not found.";
            continue;
        }

        if (kind->ToString() != "video") {
            RTC_LOG(LS_INFO) << "Skipping stats " << stats.id() << " because kind is not video.";
            continue;
        }

        if (std::string(stats.type()) == "inbound-rtp") {
            RTC_LOG(LS_INFO) << "Processing inbound-rtp stats: " << stats.id();
            ProcessInboundRTPStats(stats);
        }
    }
}

RTCStatsCollector::RTCStatsCollector() {
    RTC_LOG(LS_INFO) << "Creating RTCStatsCollector...";
}

RTCStatsCollector::~RTCStatsCollector() {
    RTC_LOG(LS_INFO) << "Destroying RTCStatsCollector...";
    Stop();  // Ensure the thread is stopped and resources are cleaned up

    if (stats_thread_.joinable()) {
        stats_thread_.join();  // Wait for the thread to finish
    }

    RTC_LOG(LS_INFO) << "RTCStatsCollector destroyed.";
}

bool RTCStatsCollector::Start(
    const std::string& filename,
    rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection) {
    std::lock_guard<std::mutex> lock(stats_mutex_);


    RTC_LOG(LS_INFO) << "RTCStatsCollector starts.";

    if (is_running_) {
        RTC_LOG(LS_INFO) << "RTCStatsCollector is already running.";
        return true;
    }

    if (!peer_connection) {
        RTC_LOG(LS_ERROR) << "No peer connection provided. Cannot start stats collection.";
        return false;
    }

    if (!OpenStatsFile(filename)) {
        RTC_LOG(LS_ERROR) << "Failed to open stats file. Cannot start stats collection.";
        return false;
    }

    peer_connection_ = peer_connection;
    should_collect_ = true;
    is_running_ = true;

    RTC_LOG(LS_INFO) << "RTCStatsCollector starts2";

    stats_thread_ = std::thread(&RTCStatsCollector::ThreadLoop, this);

    return true;
}

void RTCStatsCollector::Stop() {
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        should_collect_ = false;  // Signal the thread to stop
    }
    stop_cv_.notify_all(); // Wake up the thread if it's waiting
    RTC_LOG(LS_INFO) << "RTCStatsCollector stopped.";
}

bool RTCStatsCollector::OpenStatsFile(const std::string& filename) {
    RTC_LOG(LS_INFO) << "Opening stats file: " << filename;

    if (stats_file_.is_open()) {
        RTC_LOG(LS_INFO) << "Stats file already open.";
        return true;
    }

    stats_file_.open(filename);
    if (!stats_file_.is_open()) {
        RTC_LOG(LS_ERROR) << "Failed to open stats file: " << filename;
        return false;
    }

    RTC_LOG(LS_INFO) << "Stats file opened successfully: " << filename;
    stats_file_ << "timestamp_ms,frames_decoded,frames_dropped,frames_received,"
                << "framerate,jitter_buffer_delay_ms,video_width,video_height,"
                << "total_decode_time_ms,packets_received,decoder_implementation\n";
    stats_file_.flush();
    return true;
}

void RTCStatsCollector::CloseStatsFile() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    if (stats_file_.is_open()) {
        RTC_LOG(LS_INFO) << "Closing stats file.";
        stats_file_.flush();
        stats_file_.close();
    } else {
        RTC_LOG(LS_INFO) << "Stats file not open, nothing to close.";
    }
}

void RTCStatsCollector::ThreadLoop() {
    std::unique_lock<std::mutex> lock(stats_mutex_);
    while (should_collect_) {
        // Perform stats collection
        lock.unlock();
        CollectStats();
        lock.lock();

        // Wait for the next interval or stop signal
        stop_cv_.wait_for(lock, std::chrono::milliseconds(kStatsIntervalMs),
                          [this]() { return !should_collect_; });
    }
}

void RTCStatsCollector::CollectStats() {
    if (!peer_connection_) {
        RTC_LOG(LS_WARNING) << "No peer connection. Skipping stats collection.";
        return;
    }

    auto stats_callback = rtc::make_ref_counted<RTCStatsCollectorCallback>(
        stats_file_,
        stats_mutex_);

    peer_connection_->GetStats(stats_callback.get());
}

