#include "gst_raii.hpp"
#include <camera_pipeline.hpp>

#include <yaml-cpp/yaml.h>

#include <chrono>
#include <csignal>
#include <stdexcept>
#include <thread>

namespace
{
volatile std::sig_atomic_t g_stop = 0;
void handle_signal(int) { g_stop = 1; }

camera_stream::CameraConfig load_config(const std::string& path)
{
    const YAML::Node root = YAML::LoadFile(path)["camera_node"]["ros__parameters"];
    if (!root) {
        throw std::runtime_error("camera_node/ros__parameters not found in " + path);
    }

    camera_stream::CameraConfig cfg;
    cfg.device  = root["device"].as<std::string>(cfg.device);
    cfg.width   = root["width"].as<int>(cfg.width);
    cfg.height  = root["height"].as<int>(cfg.height);
    cfg.fps     = root["fps"].as<int>(cfg.fps);
    cfg.decoder = root["decoder"].as<std::string>(cfg.decoder);
    cfg.scaler  = root["scaler"].as<std::string>(cfg.scaler);

    cfg.gcs.width       = root["gcs_width"].as<int>(cfg.gcs.width);
    cfg.gcs.height      = root["gcs_height"].as<int>(cfg.gcs.height);
    cfg.gcs.fps         = root["gcs_fps"].as<int>(cfg.gcs.fps);
    cfg.gcs.socket_path = root["gcs_socket"].as<std::string>();

    const auto nn_widths  = root["nn_widths"].as<std::vector<int>>(std::vector<int>{});
    const auto nn_heights = root["nn_heights"].as<std::vector<int>>(std::vector<int>{});
    const auto nn_sockets = root["nn_sockets"].as<std::vector<std::string>>(std::vector<std::string>{});

    for (size_t i = 0; i < nn_sockets.size(); ++i) {
        camera_stream::NnBranch b;
        if (i < nn_widths.size())  b.width  = nn_widths[i];
        if (i < nn_heights.size()) b.height = nn_heights[i];
        b.socket_path = nn_sockets[i];
        cfg.nn.push_back(b);
    }
    return cfg;
}
}  // namespace

int main(int argc, char** argv)
{
    gst_init(&argc, &argv);
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    const std::string config_path = argc > 1 ? argv[1] : "config/camera.yaml";

    camera_stream::CameraConfig cfg;
    try {
        cfg = load_config(config_path);
    } catch (const std::exception& e) {
        g_printerr("[camera] failed to load config '%s': %s\n", config_path.c_str(), e.what());
        return 1;
    }

    camera_stream::CameraPipeline pipeline(std::move(cfg));
    pipeline.start();

    while (!g_stop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    pipeline.stop();
    gst_deinit();
    return 0;
}
