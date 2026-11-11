#include "usbcapture/capture.h"

namespace usbcapture {

Version version() {
    return {0, 1, 0};
}

bool isBackendAvailable() {
    // Flipped to true when the QuickTime capture spike lands.
    return false;
}

}  // namespace usbcapture
