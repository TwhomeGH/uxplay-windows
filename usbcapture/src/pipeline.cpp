// GStreamer pipeline for the USB capture stream.
//
// TODO: implement after the spike. Build an appsrc -> h264parse -> decodebin
// pipeline (and the audio branch) that reuses the same sinks as the AirPlay
// renderer, or links the existing renderers target if its API fits.

namespace usbcapture {
namespace {
constexpr int kPipelinePlaceholder = 0;
}  // namespace
}  // namespace usbcapture
