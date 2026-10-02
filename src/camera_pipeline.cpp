#include <camera_pipeline.hpp>
namespace camera_stream
{
    void CameraPipeline::supervise()
    {
        using clock = std::chrono::steady_clock;
        using ms = std::chrono::milliseconds;
        ms backoff{500};

        while (!stop_) {
            Session s;
            const auto started = clock::now();

            bool ok = build(s) &&
                gst_element_set_state(s.pipeline.get(), GST_STATE_PLAYING)
                    != GST_STATE_CHANGE_FAILURE;
            if (ok) {
                emit("pipeline started");
                watch(s);
            }

            s.bus.reset();
            s.pipeline.reset();
            {
                std::lock_guard<std::mutex> lk(m_);
                stats_.playing = false;
            }
            if (stop_) break;

            ++restarts_;
            backoff = (clock::now() - started > std::chrono::seconds(10))
                          ? ms{500}
                          : std::min(backoff * 2, ms{5000});
            emit("restarting in " + std::to_string(backoff.count()) + " ms");
            for (ms w{0}; w < backoff && !stop_; w += ms{100})
                std::this_thread::sleep_for(ms{100});
        }
    }

    bool CameraPipeline::build(Session & s)
    {
        ::unlink(cfg_.gcs.socket_path.c_str());
        for (const auto& b : cfg_.nn) ::unlink(b.socket_path.c_str());

        GError* err = nullptr;
        GstElement* p = gst_parse_launch_full(make_pipeline_desc(cfg_).c_str(), nullptr,
                                              GST_PARSE_FLAG_FATAL_ERRORS, &err);
        if (!p) {
            emit(std::string("parse error: ") + (err ? err->message : "unknown"));
            g_clear_error(&err);
            return false;
        }
        g_clear_error(&err);
        s.pipeline.reset(p);
        s.bus.reset(gst_element_get_bus(p));

        add_probe(p, "t",     "sink", cnt_in_);
        add_probe(p, "q_gcs", "src",  cnt_gcs_);
        for (size_t i = 0; i < cnt_nn_.size(); ++i)
            add_probe(p, ("q_nn" + std::to_string(i)).c_str(), "src", cnt_nn_[i]);
        return true;
    }

    void CameraPipeline::add_probe(GstElement* pipeline, const char* elem, const char* pad,
                   std::atomic<uint64_t>& counter)
    {
        gstx::ElementPtr e(gst_bin_get_by_name(GST_BIN(pipeline), elem));
        if (!e) return;
        gstx::PadPtr p(gst_element_get_static_pad(e.get(), pad));
        if (!p) return;
        gst_pad_add_probe(p.get(), GST_PAD_PROBE_TYPE_BUFFER, &on_buffer, &counter, nullptr);
    }

    void CameraPipeline::watch(Session& s)
    {
        using clock = std::chrono::steady_clock;
        using namespace std::chrono_literals;

        auto last_tick = clock::now();
        auto last_progress = last_tick;
        uint64_t p_in = cnt_in_.load(), p_gcs = cnt_gcs_.load();
        std::vector<uint64_t> p_nn(cnt_nn_.size());
        for (size_t i = 0; i < p_nn.size(); ++i) p_nn[i] = cnt_nn_[i].load();

        while (!stop_) {
            GstMessage* m = gst_bus_timed_pop_filtered(
                s.bus.get(), 200 * GST_MSECOND,
                (GstMessageType)(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
            if (m) {
                if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR) {
                    GError* e = nullptr; gchar* dbg = nullptr;
                    gst_message_parse_error(m, &e, &dbg);
                    emit(std::string("GStreamer error: ") + e->message +
                         " (" + (dbg ? dbg : "") + ")");
                    g_clear_error(&e); g_free(dbg);
                } else {
                    emit("EOS in camera pipeline");
                }
                gst_message_unref(m);
                return;
            }

            const auto now = clock::now();
            if (now - last_tick < 1s) continue;
            const double dt = std::chrono::duration<double>(now - last_tick).count();
            last_tick = now;

            Stats st;
            st.playing  = true;
            st.restarts = restarts_.load();
            const uint64_t c_in = cnt_in_.load(), c_gcs = cnt_gcs_.load();
            st.fps_in  = double(c_in  - p_in)  / dt;
            st.fps_gcs = double(c_gcs - p_gcs) / dt;
            for (size_t i = 0; i < p_nn.size(); ++i) {
                const uint64_t c = cnt_nn_[i].load();
                st.fps_nn.push_back(double(c - p_nn[i]) / dt);
                p_nn[i] = c;
            }
            if (c_in != p_in) last_progress = now;
            p_in = c_in; p_gcs = c_gcs;
            {
                std::lock_guard<std::mutex> lk(m_);
                stats_ = st;
            }

            if (now - last_progress > 5s) {
                emit("no frames from camera for 5 s");
                return;
            }
        }
    }

    void CameraPipeline::emit(const std::string & msg)
    {
        if (on_event_) on_event_(msg);
        else g_printerr("[camera] %s\n", msg.c_str());
    }
    
} // namespace name
