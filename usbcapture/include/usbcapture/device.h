#pragma once

#include <string>
#include <vector>

namespace usbcapture {

/// USB-connected iOS device. Populated from usbmuxd/libimobiledevice once
/// the enumeration path is implemented.
struct DeviceInfo {
    std::string udid;
    std::string name;
    std::string productType;
    std::string iosVersion;
};

struct DeviceList {
    std::vector<DeviceInfo> devices;
    std::string error;
};

/// Enumerate devices visible through the Apple Mobile Device service.
/// The skeleton returns an empty list.
DeviceList listDevices();

}  // namespace usbcapture
