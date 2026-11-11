#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace usbcapture {

/// Encoded H.264 access unit. The pointer is valid only for the duration of
/// the callback; the pipeline copies or hands it to GStreamer immediately.
struct VideoFrame {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::int64_t pts = 0;
};

/// Encoded or PCM audio payload, depending on the negotiated QuickTime
/// format. The implementation will document the format before use.
struct AudioFrame {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::int64_t pts = 0;
};

struct SessionConfig {
    std::string udid;
    bool audio = true;
};

class Session {
public:
    using VideoCallback = std::function<void(const VideoFrame&)>;
    using AudioCallback = std::function<void(const AudioFrame&)>;

    virtual ~Session() = default;

    virtual bool start(const SessionConfig& config,
                       VideoCallback onVideo,
                       AudioCallback onAudio,
                       std::string* error) = 0;
    virtual void stop() = 0;
};

/// Returns nullptr while the backend is a skeleton.
std::unique_ptr<Session> createSession();

}  // namespace usbcapture
