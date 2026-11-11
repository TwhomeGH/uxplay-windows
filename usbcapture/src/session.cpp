#include "usbcapture/session.h"

namespace usbcapture {

std::unique_ptr<Session> createSession() {
    // TODO: return the QuickTime capture session once the protocol spike
    // lands. Until then the UI must treat the backend as unavailable.
    return nullptr;
}

}  // namespace usbcapture
