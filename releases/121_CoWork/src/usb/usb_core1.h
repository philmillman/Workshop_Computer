// Core 1 entry: decides the USB electrical role from the USB-C power state
// (150 ms settle, ComputerCard midi_device_host example pattern), brings up
// TinyUSB in that role, then pumps USB / MIDI / CDC / config forever.

#ifndef COWORK_USB_CORE1_H
#define COWORK_USB_CORE1_H

namespace cowork {

// Core-1 entry point for multicore_launch_core1(); never returns.
void UsbCore1Entry();

} // namespace cowork

#endif
