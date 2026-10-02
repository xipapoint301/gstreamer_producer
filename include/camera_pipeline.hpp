#pragma once
#include "gst_raii.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

namespace camera_stream
{


struct NnBranch {
    int width = 640;
    int height = 640;
    std::string socket_path;
};

// Параметры согласованы с дефолтами VideoStreamConfig потребителя (gcs_communication::VideoStreamer)
struct GcsBranch {
    int width = 1280, height = 720, fps = 15;
    std::string socket_path;
};

struct CameraConfig {
    std::string device = "/dev/video0";
    int width = 1920, height = 1080, fps = 30;

    std::string decoder = "mppjpegdec";
    std::string scaler  = "videoscale ! videoconvert";

    GcsBranch gcs;
    std::vector<NnBranch> nn;
};

inline std::string make_pipeline_desc(const CameraConfig& c) {
    std::ostringstream s;
    s << "v4l2src device=" << c.device
      << " ! image/jpeg,width=" << c.width << ",height=" << c.height
      << ",framerate=" << c.fps << "/1"
      << " ! " << c.decoder
      << " ! tee name=t ";

    // Ветка GCS. leaky: медленная ветка не должна останавливать tee
    // Сырой кадр отдаём через shmsink (как и NN-ветки) — кодированием в H.264/JPEG
    // и отправкой по UDP занимается потребитель (gcs_communication::VideoStreamer)
    {
        const auto& g = c.gcs;
        const size_t frame = size_t(g.width) * g.height * 3 / 2;   // I420: 12 бит/пиксель
        s << "t. ! queue name=q_gcs leaky=downstream max-size-buffers=2"
          << " max-size-time=0 max-size-bytes=0"
          << " ! " << c.scaler << " ! videorate"
          << " ! video/x-raw,format=I420,width=" << g.width << ",height=" << g.height
          << ",framerate=" << g.fps << "/1"
          << " ! shmsink socket-path=" << g.socket_path
          << " shm-size=" << frame * 8                            // запас на ~8 кадров
          << " wait-for-connection=false sync=false ";
    }

    // Ветки нейросетей: по одной shm-ветке на каждый размер
    for (size_t i = 0; i < c.nn.size(); ++i) {
        const auto& b = c.nn[i];
        const size_t frame = size_t(b.width) * b.height * 3;     // RGB
        s << "t. ! queue name=q_nn" << i
          << " leaky=downstream max-size-buffers=1 max-size-time=0 max-size-bytes=0"
          << " ! " << c.scaler
          << " ! video/x-raw,format=RGB,width=" << b.width << ",height=" << b.height
          << " ! shmsink socket-path=" << b.socket_path
          << " shm-size=" << frame * 8                            // запас на ~8 кадров
          << " wait-for-connection=false sync=false ";
    }
    return s.str();
}

class CameraPipeline {
public:
    struct Stats {
        bool playing = false;
        uint32_t restarts = 0;
        double fps_in = 0, fps_gcs = 0;
        std::vector<double> fps_nn;
    };
    using EventCb = std::function<void(const std::string&)>;

    explicit CameraPipeline(CameraConfig cfg, EventCb on_event = {})
        : cfg_(std::move(cfg)), on_event_(std::move(on_event)) {
        for (size_t i = 0; i < cfg_.nn.size(); ++i) cnt_nn_.emplace_back(0);
    }
    CameraPipeline(const CameraPipeline&) = delete;
    CameraPipeline& operator=(const CameraPipeline&) = delete;
    ~CameraPipeline() { stop(); }

    void start() { thread_ = std::thread([this] { supervise(); }); }

    void stop() {
        stop_ = true;
        if (thread_.joinable()) thread_.join();
    }

    Stats stats() const {
        std::lock_guard<std::mutex> lk(m_);
        return stats_;
    }

private:
    struct Session {
        gstx::PipelinePtr pipeline;
        gstx::BusPtr      bus;
    };

    void supervise();
    bool build(Session& s);

    void add_probe(GstElement* pipeline, const char* elem, const char* pad,
                   std::atomic<uint64_t>& counter);

    static GstPadProbeReturn on_buffer(GstPad*, GstPadProbeInfo*, gpointer d) {
        static_cast<std::atomic<uint64_t>*>(d)->fetch_add(1, std::memory_order_relaxed);
        return GST_PAD_PROBE_OK;
    }

    void watch(Session& s);

    void emit(const std::string& msg);

    CameraConfig cfg_;
    EventCb on_event_;
    std::atomic<bool> stop_{false};
    std::atomic<uint32_t> restarts_{0};
    std::thread thread_;

    std::atomic<uint64_t> cnt_in_{0}, cnt_gcs_{0};
    std::deque<std::atomic<uint64_t>> cnt_nn_;   // deque: ссылки на элементы остаются валидными

    mutable std::mutex m_;
    Stats stats_;
};

}