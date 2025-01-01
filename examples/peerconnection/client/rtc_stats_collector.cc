#include "examples/peerconnection/client/rtc_stats_collector.h"
#include "rtc_base/logging.h"
#include "rtc_base/time_utils.h"

RTCStatsCollectorCallback::RTCStatsCollectorCallback(
    std::ofstream& per_frame_stats_file,
    std::ofstream& average_stats_file,
    std::mutex& stats_mutex,
    PersistentStats& persistent_stats)  // Add persistent stats
    : per_frame_stats_file_(per_frame_stats_file),
        average_stats_file_(average_stats_file),
        stats_mutex_(stats_mutex),
        persistent_stats_(persistent_stats) {
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


template <typename T>
bool safe_parse(const std::string& str, T* out) {
    static_assert(std::is_integral<T>::value, "safe_parse requires integral types");
    char* end = nullptr;
    int64_t value = std::strtoll(str.c_str(), &end, 10);
    if (end == nullptr || *end != '\0') {
        return false;
    }
    if (value < std::numeric_limits<T>::min() || value > std::numeric_limits<T>::max()) {
        return false;
    }
    *out = static_cast<T>(value);
    return true;
}

bool ParseTimingFrameInfo(const std::string& timing_info_str, webrtc::TimingFrameInfo* timing_info) {
    if (!timing_info || timing_info_str.empty()) {
        return false;
    }

    std::istringstream iss(timing_info_str);
    std::vector<std::string> tokens;
    std::string token;

    // Split the string into tokens by comma
    while (std::getline(iss, token, ',')) {
        tokens.push_back(token);
    }

    if (tokens.size() != 16) {  // Adjust for 16 fields in the log
        RTC_LOG(LS_ERROR) << "Invalid timing info string: " << timing_info_str;
        return false;
    }

    // Use safe_parse to handle the parsing
    if (!safe_parse(tokens[0], &timing_info->rtp_timestamp) ||
        !safe_parse(tokens[1], &timing_info->capture_time_ms) ||
        !safe_parse(tokens[2], &timing_info->encode_start_ms) ||
        !safe_parse(tokens[3], &timing_info->encode_finish_ms) ||
        !safe_parse(tokens[4], &timing_info->packetization_finish_ms) ||
        !safe_parse(tokens[5], &timing_info->pacer_exit_ms) ||
        !safe_parse(tokens[6], &timing_info->network_timestamp_ms) ||
        !safe_parse(tokens[7], &timing_info->network2_timestamp_ms) ||
        !safe_parse(tokens[8], &timing_info->receive_start_ms) ||
        !safe_parse(tokens[9], &timing_info->receive_finish_ms) ||
        !safe_parse(tokens[10], &timing_info->decode_start_ms) ||
        !safe_parse(tokens[11], &timing_info->decode_finish_ms) ||
        !safe_parse(tokens[12], &timing_info->render_time_ms)) {// bool-like integer
        RTC_LOG(LS_ERROR) << "Error parsing timing info string: " << timing_info_str;
        return false;
    }

    return true;
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

    const auto* timing_info_attr = find_attribute("googTimingFrameInfo");
    
    std::lock_guard<std::mutex> lock(stats_mutex_); 
    if (timing_info_attr) {
         // Extract the timing info as a string
        std::string timing_info_str = timing_info_attr->ToString();

        // Parse the timing info string into TimingFrameInfo
        webrtc::TimingFrameInfo timing_info;
        if (ParseTimingFrameInfo(timing_info_str, &timing_info) && timing_info.encode_start_ms > 10000) {
            // Calculate timing stages
            int64_t encoding_ms = (timing_info.encode_finish_ms >= 0 && timing_info.encode_start_ms >= 0)
                                    ? timing_info.encode_finish_ms - timing_info.encode_start_ms
                                    : -1;

            int64_t network_ms = (timing_info.network2_timestamp_ms >= 0 && timing_info.pacer_exit_ms >= 0)
                                    ? timing_info.network2_timestamp_ms - timing_info.pacer_exit_ms
                                    : -1;

            int64_t decoding_ms = (timing_info.decode_finish_ms >= 0 && timing_info.decode_start_ms >= 0)
                                    ? timing_info.decode_finish_ms - timing_info.decode_start_ms
                                    : -1;

            int64_t rendering_ms = (timing_info.render_time_ms >= 0 && timing_info.decode_finish_ms >= 0)
                                    ? timing_info.render_time_ms - timing_info.decode_finish_ms
                                    : -1;

            int64_t e2e_ms = (timing_info.capture_time_ms >= 0 && timing_info.decode_finish_ms >= 0)
                                ? timing_info.decode_finish_ms - timing_info.capture_time_ms
                                : -1;

            // Calculate inter-frame timing (delta between current and last render_time_ms)
            int64_t inter_frame_ms = -1;
            if (persistent_stats_.last_render_time_ms_ >= 0 && timing_info.render_time_ms >= 0) {
                inter_frame_ms = timing_info.render_time_ms - persistent_stats_.last_render_time_ms_;
            }
            persistent_stats_.last_render_time_ms_ = timing_info.render_time_ms;

            // Calculate intra-frame construction time (delta between receive_finish_ms and receive_start_ms)
            int64_t intra_construction_ms = (timing_info.receive_finish_ms >= 0 && timing_info.receive_start_ms >= 0)
                                                ? timing_info.receive_finish_ms - timing_info.receive_start_ms
                                                : -1;

            // Log the frame timings to the file
            {
                if (per_frame_stats_file_.is_open()) {
                    per_frame_stats_file_ << rtc::TimeMillis() << "," << timing_info.rtp_timestamp << ","
                                        << encoding_ms << ","
                                        << network_ms << ","
                                        << decoding_ms << ","
                                        << rendering_ms << ","
                                        << e2e_ms << ","
                                        << inter_frame_ms << ","
                                        << intra_construction_ms << "\n";
                    per_frame_stats_file_.flush();
                }
            }
            // Update the last processed timestamp
            persistent_stats_.last_timestamp_ = timing_info.rtp_timestamp;
        }
    }


    persistent_stats_.frame_timing_count_ += 1;

    // Extract all stats with null handling
    int64_t frames_decoded = static_cast<int64_t>(get_numeric("framesDecoded"));
    int64_t frames_dropped = static_cast<int64_t>(get_numeric("framesDropped"));
    int64_t frames_received = static_cast<int64_t>(get_numeric("framesReceived"));
    double framerate = get_numeric("framesPerSecond");
    double jitter_buffer_delay = get_numeric("jitterBufferDelay") * 1000.0;
    int64_t width = static_cast<int64_t>(get_numeric("frameWidth"));
    int64_t height = static_cast<int64_t>(get_numeric("frameHeight"));
    double total_decode_time = get_numeric("totalDecodeTime") * 1000.0;
    int64_t bytes_received = static_cast<int64_t>(get_numeric("bytesReceived"));

    int64_t current_time_ms = rtc::TimeMillis();

    // Initialize first stats time if not set
    if (persistent_stats_.first_stats_time_ms_ == -1) {
        persistent_stats_.first_stats_time_ms_ = current_time_ms;
        persistent_stats_.period_start_time_ms_ = current_time_ms;
        persistent_stats_.period_start_bytes_ = bytes_received;
        persistent_stats_.total_bytes_received_ = bytes_received;
    }

    // Accumulate values
    persistent_stats_.acc_frames_decoded_ += frames_decoded;
    persistent_stats_.acc_frames_dropped_ += frames_dropped;
    persistent_stats_.acc_frames_received_ += frames_received;
    persistent_stats_.acc_framerate_ += framerate;
    persistent_stats_.acc_jitter_buffer_delay_ += jitter_buffer_delay;
    persistent_stats_.acc_total_decode_time_ += total_decode_time;
    persistent_stats_.acc_count_++;

    bool should_write = (current_time_ms - persistent_stats_.last_average_time_ms_) >= 1000;  // 1 second interval

    if (should_write && persistent_stats_.acc_count_ > 0) {
        // Calculate overall average bitrate (since start)
        double overall_time_sec = (current_time_ms - persistent_stats_.first_stats_time_ms_) / 1000.0;
        double overall_bytes_delta = bytes_received;
        double overall_average_bitrate = 0.0;
        if (overall_time_sec > 0) {
            overall_average_bitrate = (overall_bytes_delta * 8.0) / overall_time_sec;  // bits per second
        }
        persistent_stats_.total_bytes_received_ = bytes_received;

        // Calculate current period average bitrate
        double period_time_sec = (current_time_ms - persistent_stats_.period_start_time_ms_) / 1000.0;
        double period_bytes_delta = bytes_received - persistent_stats_.period_start_bytes_;
        double period_average_bitrate = 0.0;
        if (period_time_sec > 0) {
            period_average_bitrate = (period_bytes_delta * 8.0) / period_time_sec;  // bits per second
        }

        // Calculate averages
        double avg_frames_decoded = static_cast<double>(persistent_stats_.acc_frames_decoded_) / persistent_stats_.acc_count_;
        double avg_frames_dropped = static_cast<double>(persistent_stats_.acc_frames_dropped_) / persistent_stats_.acc_count_;
        double avg_frames_received = static_cast<double>(persistent_stats_.acc_frames_received_) / persistent_stats_.acc_count_;
        double avg_framerate = persistent_stats_.acc_framerate_ / persistent_stats_.acc_count_;
        double avg_jitter_buffer_delay = persistent_stats_.acc_jitter_buffer_delay_ / persistent_stats_.acc_count_;
        double avg_total_decode_time = persistent_stats_.acc_total_decode_time_ / persistent_stats_.acc_count_;

        // Handle decoder implementation
        const auto* decoder_impl_attr = find_attribute("decoderImplementation");
        std::string decoder_implementation =
            decoder_impl_attr ? decoder_impl_attr->ToString() : "unknown";
        if (decoder_implementation == "null") {
            decoder_implementation = "unknown";
        }

        // Write stats to file with mutex protection
        if (average_stats_file_.is_open()) {
            average_stats_file_ << current_time_ms << ","
                    << avg_frames_decoded << ","
                    << avg_frames_dropped << ","
                    << avg_frames_received << ","
                    << avg_framerate << ","
                    << avg_jitter_buffer_delay << ","
                    << width << ","
                    << height << ","
                    << avg_total_decode_time << ","
                    << bytes_received << ","
                    << period_average_bitrate << ","  // Current period bitrate
                    << overall_average_bitrate << ","  // Overall average bitrate
                    << decoder_implementation << "\n";
            average_stats_file_.flush();
        }

        // Reset accumulators
        persistent_stats_.acc_frames_decoded_ = 0;
        persistent_stats_.acc_frames_dropped_ = 0;
        persistent_stats_.acc_frames_received_ = 0;
        persistent_stats_.acc_framerate_ = 0.0;
        persistent_stats_.acc_jitter_buffer_delay_ = 0.0;
        persistent_stats_.acc_total_decode_time_ = 0.0;
        persistent_stats_.acc_count_ = 0;
        persistent_stats_.last_average_time_ms_ = current_time_ms;

        // Reset period accumulators
        persistent_stats_.period_start_time_ms_ = current_time_ms;
        persistent_stats_.period_start_bytes_ = bytes_received;
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
    const std::string& foldername,
    rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection) {


    RTC_LOG(LS_INFO) << "RTCStatsCollector starts.";

    if (is_running_) {
        RTC_LOG(LS_INFO) << "RTCStatsCollector is already running.";
        return true;
    }

    if (!peer_connection) {
        RTC_LOG(LS_ERROR) << "No peer connection provided. Cannot start stats collection.";
        return false;
    }

    if (!OpenStatsFile(foldername)) {
        RTC_LOG(LS_ERROR) << "Failed to open stats file. Cannot start stats collection.";
        return false;
    }

    peer_connection_ = peer_connection;
    should_collect_ = true;
    is_running_ = true;

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

bool RTCStatsCollector::OpenStatsFile(const std::string& foldername) {
    RTC_LOG(LS_INFO) << "Opening stats files in folder: " << foldername;

    // Construct file paths for per-frame and average stats files
    std::string per_frame_filename = foldername + "/per_frame_stats.csv";
    std::string average_filename = foldername + "/average_stats.csv";

    // Open per-frame stats file
    /*
    per_frame_stats_file_.open(per_frame_filename);
    if (!per_frame_stats_file_.is_open()) {
        RTC_LOG(LS_ERROR) << "Failed to open per-frame stats file: " << per_frame_filename;
        return false;
    }
    RTC_LOG(LS_INFO) << "Per-frame stats file opened successfully: " << per_frame_filename;
    per_frame_stats_file_ << "timestamp_ms,rtp_timestamp,encoding_ms,network_ms,decoding_ms,rendering_ms,e2e_ms,"
                          << "inter_frame_ms,intra_construction_ms\n";
    per_frame_stats_file_.flush();
    */

    // Open average stats file
    average_stats_file_.open(average_filename);
    if (!average_stats_file_.is_open()) {
        RTC_LOG(LS_ERROR) << "Failed to open average stats file: " << average_filename;
        per_frame_stats_file_.close();  // Cleanup already opened file
        return false;
    }
    RTC_LOG(LS_INFO) << "Average stats file opened successfully: " << average_filename;
    average_stats_file_ << "timestamp_ms,frames_decoded,frames_dropped,frames_received,"
                          << "framerate,jitter_buffer_delay_ms,video_width,video_height,"
                          << "total_decode_time_ms,total_bytes_received,bitrates,overall_avg_bitrates,decoder_implementation\n";
    average_stats_file_.flush();

    return true;
}

void RTCStatsCollector::CloseStatsFile() {
    if (per_frame_stats_file_.is_open()) {
        RTC_LOG(LS_INFO) << "Closing per-frame stats file.";
        per_frame_stats_file_.flush();
        per_frame_stats_file_.close();
    }

    if (average_stats_file_.is_open()) {
        RTC_LOG(LS_INFO) << "Closing average stats file.";
        average_stats_file_.flush();
        average_stats_file_.close();
    }
}

void RTCStatsCollector::ThreadLoop() {
    std::unique_lock<std::mutex> lock(stats_mutex_);
    while (should_collect_) {
        lock.unlock();  // Explicit unlock
        CollectStats();
        lock.lock();  // Explicit lock before waiting

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
        per_frame_stats_file_,
        average_stats_file_,
        stats_mutex_,
        persistent_stats_); 

    peer_connection_->GetStats(stats_callback.get());
}

