#pragma once
#include <gst/gst.h>
#include <memory>

namespace gstx {
template <class T> struct Unref {
    void operator()(T* p) const { if (p) gst_object_unref(p); }
};
struct SampleUnref {
    void operator()(GstSample* s) const { if (s) gst_sample_unref(s); }
};
struct PipelineStop {
    void operator()(GstElement* p) const {
        if (!p) return;
        gst_element_set_state(p, GST_STATE_NULL);
        gst_object_unref(p);
    }
};
using PipelinePtr = std::unique_ptr<GstElement, PipelineStop>;
using ElementPtr  = std::unique_ptr<GstElement, Unref<GstElement>>;
using BusPtr      = std::unique_ptr<GstBus,     Unref<GstBus>>;
using PadPtr      = std::unique_ptr<GstPad,     Unref<GstPad>>;
using SamplePtr   = std::unique_ptr<GstSample,  SampleUnref>;
}  // namespace gstx
