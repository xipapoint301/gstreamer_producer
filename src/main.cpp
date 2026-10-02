#include "gst_raii.hpp"
#include <camera_pipeline.hpp>

#include <chrono>
#include <csignal>
#include <thread>

namespace
{
volatile std::sig_atomic_t g_stop = 0;
void handle_signal(int) { g_stop = 1; }
}  // namespace

int main(int argc, char** argv)
{
    gst_init(&argc, &argv);
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    // TODO: читать из config/camera.yaml, пока значения захардкожены по его образцу
    camera_stream::CameraConfig cfg;
    cfg.device = "/dev/video0";
    cfg.width = 1920;
    cfg.height = 1080;
    cfg.fps = 30;
    cfg.decoder = "mppjpegdec";
    cfg.scaler = "videoscale ! videoconvert";

    cfg.gcs.width = 1280;
    cfg.gcs.height = 720;
    cfg.gcs.fps = 15;
    cfg.gcs.socket_path = "/tmp/cam_gcs.sock";

    cfg.nn.push_back({640, 640, "/tmp/cam_nn0.sock"});
    cfg.nn.push_back({640, 640, "/tmp/cam_nn1.sock"});

    camera_stream::CameraPipeline pipeline(std::move(cfg));
    pipeline.start();

    while (!g_stop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    pipeline.stop();
    gst_deinit();
    return 0;
}
