"""Enumerate the Apple USB composite device.

Read-only: reads descriptors through libusb/pyusb. It never claims an
interface, changes the active configuration, or touches the Apple driver.
"""

import sys

import usb.core

VID = 0x05AC
PID = 0x12AB


def main() -> int:
    devices = list(usb.core.find(find_all=True, idVendor=VID, idProduct=PID))
    if not devices:
        print(f"no device {VID:04x}:{PID:04x}")
        return 1
    for dev in devices:
        print(
            f"device {dev.idVendor:04x}:{dev.idProduct:04x}"
            f" bus={dev.bus} addr={dev.address}"
        )
        print(f"  bcdUSB={dev.bcdUSB:04x} class={dev.bDeviceClass:02x}")
        try:
            configurations = list(dev)
        except usb.core.USBError as exc:
            print(f"  cannot read configurations: {exc}")
            continue
        for cfg in configurations:
            print(
                f"  config {cfg.bConfigurationValue}"
                f" ({cfg.bNumInterfaces} interfaces)"
            )
            for intf in cfg:
                print(
                    f"    interface {intf.bInterfaceNumber}"
                    f".{intf.bAlternateSetting}"
                    f" class={intf.bInterfaceClass:02x}"
                    f" subclass={intf.bInterfaceSubClass:02x}"
                    f" protocol={intf.bInterfaceProtocol:02x}"
                )
                for ep in intf:
                    print(
                        f"      endpoint {ep.bEndpointAddress:02x}"
                        f" attributes={ep.bmAttributes:02x}"
                        f" max_packet={ep.wMaxPacketSize}"
                    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
