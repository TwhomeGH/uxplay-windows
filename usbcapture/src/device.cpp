#include "usbcapture/device.h"

namespace usbcapture {

DeviceList listDevices() {
    // TODO: enumerate devices through usbmuxd/libimobiledevice after the
    // QuickTime capture spike proves the transport on Windows.
    return {};
}

}  // namespace usbcapture
