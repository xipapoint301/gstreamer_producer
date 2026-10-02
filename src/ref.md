#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

#include "gst_raii.hpp"
#include "video_quality.hpp"

namespace gcs_communication
{

struct VideoStreamConfig
{
  std::string socket_path;
  Resolution source_size{1280, 720};
  std::string source_format{"I420"};
  int fps{15};

  std::string host{"127.0.0.1"};
  std::uint16_t port{5600};

  VideoQuality quality{VideoQuality::P480};
  int jpeg_quality{80};

  std::chrono::milliseconds restart_backoff_min{200};
  std::chrono::milliseconds restart_backoff_max{3000};
};

class VideoStreamer
{
public:
  explicit VideoStreamer(VideoStreamConfig config);
  ~VideoStreamer();

  VideoStreamer(const VideoStreamer &) = delete;
  VideoStreamer & operator=(const VideoStreamer &) = delete;

  void start();
  void stop();

  void set_quality(VideoQuality quality);

  void tick();

private:
  std::string description() const;
  bool open_locked();
  void close_locked();
  void schedule_restart_locked();

  using Clock = std::chrono::steady_clock;

  VideoStreamConfig config_;
  std::mutex mutex_;
  bool running_{false};
  gstx::PipelinePtr pipeline_;
  gstx::BusPtr bus_;
  std::chrono::milliseconds backoff_;
  Clock::time_point next_retry_{};
  Clock::time_point opened_at_{};
};

}  // namespace gcs_communication



#include "gstreamer.hpp"

#include <algorithm>
#include <sstream>

namespace gcs_communication
{

namespace
{
constexpr std::chrono::seconds kHealthyUptime{5};
}

VideoStreamer::VideoStreamer(VideoStreamConfig config)
: config_(std::move(config)), backoff_(config_.restart_backoff_min)
{
}

VideoStreamer::~VideoStreamer()
{
  stop();
}

void VideoStreamer::start()
{
  std::lock_guard<std::mutex> lock(mutex_);
  running_ = true;
  next_retry_ = Clock::now();
  if (!open_locked()) {
    schedule_restart_locked();
  }
}

void VideoStreamer::stop()
{
  std::lock_guard<std::mutex> lock(mutex_);
  running_ = false;
  close_locked();
}

void VideoStreamer::set_quality(VideoQuality quality)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (quality == config_.quality) {
    return;
  }
  config_.quality = quality;
  if (!running_) {
    return;
  }

  const Resolution res = get_resolution(quality);
  g_printerr("[video] quality -> %dx%d, recreating pipeline\n", res.width, res.height);
  close_locked();
  backoff_ = config_.restart_backoff_min;
  if (!open_locked()) {
    schedule_restart_locked();
  }
}

void VideoStreamer::tick()
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!running_) {
    return;
  }

  if (!pipeline_) {
    if (Clock::now() >= next_retry_ && !open_locked()) {
      schedule_restart_locked();
    }
    return;
  }

  GstMessage * msg = gst_bus_pop_filtered(
    bus_.get(), static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
  if (msg == nullptr) {
    return;
  }

  if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
    GError * err = nullptr;
    gst_message_parse_error(msg, &err, nullptr);
    g_printerr("[video] pipeline error: %s\n", err ? err->message : "unknown");
    g_clear_error(&err);
  } else {
    g_printerr("[video] end of stream (producer gone?), restarting\n");
  }
  gst_message_unref(msg);

  if (Clock::now() - opened_at_ > kHealthyUptime) {
    backoff_ = config_.restart_backoff_min;
  }
  close_locked();
  schedule_restart_locked();
}

std::string VideoStreamer::description() const
{
  const Resolution out = get_resolution(config_.quality);
  std::ostringstream s;
  s << "shmsrc socket-path=" << config_.socket_path << " is-live=true do-timestamp=true"
    << " ! video/x-raw,format=" << config_.source_format
    << ",width=" << config_.source_size.width << ",height=" << config_.source_size.height
    << ",framerate=" << config_.fps << "/1"
    << " ! queue leaky=downstream max-size-buffers=2"
    << " ! videoscale ! video/x-raw,width=" << out.width << ",height=" << out.height
    << " ! videoconvert"
    << " ! jpegenc quality=" << config_.jpeg_quality
    << " ! rtpjpegpay"
    << " ! udpsink host=" << config_.host << " port=" << config_.port
    << " sync=false async=false";
  return s.str();
}

bool VideoStreamer::open_locked()
{
  GError * err = nullptr;
  GstElement * raw = gst_parse_launch(description().c_str(), &err);
  if (raw == nullptr) {
    g_printerr("[video] parse error: %s\n", err ? err->message : "unknown");
    g_clear_error(&err);
    return false;
  }
  g_clear_error(&err);

  pipeline_.reset(raw);
  bus_.reset(gst_element_get_bus(raw));
  opened_at_ = Clock::now();

  if (gst_element_set_state(raw, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    close_locked();
    return false;
  }
  return true;
}

void VideoStreamer::close_locked()
{
  bus_.reset();
  pipeline_.reset();
}

void VideoStreamer::schedule_restart_locked()
{
  next_retry_ = Clock::now() + backoff_;
  backoff_ = std::min(backoff_ * 2, config_.restart_backoff_max);
}

}  // namespace gcs_communication
