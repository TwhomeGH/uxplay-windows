#pragma once

namespace usbcapture {

struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
};

/// Backend version. The protocol implementation is added after the spike.
Version version();

/// True only when the QuickTime capture backend can enumerate and capture.
/// The skeleton returns false so the UI can keep the USB mode hidden.
bool isBackendAvailable();

}  // namespace usbcapture
