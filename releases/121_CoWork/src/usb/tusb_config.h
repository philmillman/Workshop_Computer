// TinyUSB configuration: dual-role (device OR host on rhport 0, chosen at
// boot from the USB-C power state), composite MIDI + CDC device.
//
// Based on the ComputerCard midi_device_host example config plus the MLRws
// CDC device config. NOTE: do NOT #define CFG_TUH_MIDI 1 — the vendored
// rppicomidi usb_midi_host driver registers itself as an application driver,
// and a fragment in TinyUSB's usbh.c breaks the build if CFG_TUH_MIDI is set.

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------
// COMMON
//--------------------------------------------------------------------

#ifndef CFG_TUSB_MCU
  #error CFG_TUSB_MCU must be defined
#endif

#define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_HOST | OPT_MODE_DEVICE)

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS                 OPT_OS_NONE
#endif

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN          __attribute__((aligned(4)))
#endif

//--------------------------------------------------------------------
// DEVICE (composite MIDI + CDC)
//--------------------------------------------------------------------

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE      64
#endif

#define CFG_TUD_MIDI                1
#define CFG_TUD_CDC                 1
#define CFG_TUD_HID                 0
#define CFG_TUD_MSC                 0
#define CFG_TUD_VENDOR              0

#define CFG_TUD_MIDI_RX_BUFSIZE     128
#define CFG_TUD_MIDI_TX_BUFSIZE     128

// Large CDC buffers keep sample uploads fast
#define CFG_TUD_CDC_RX_BUFSIZE      512
#define CFG_TUD_CDC_TX_BUFSIZE      512

//--------------------------------------------------------------------
// HOST (USB MIDI via the vendored rppicomidi app driver)
//--------------------------------------------------------------------

// Enlarged from default 256 to cope with long enumerations from some devices
#define CFG_TUH_ENUMERATION_BUFSIZE 1024

#define CFG_TUH_HUB                 1
#define CFG_TUH_CDC                 0
#define CFG_TUH_HID                 0
#define CFG_TUH_MSC                 0
#define CFG_TUH_VENDOR              0

#define CFG_TUH_DEVICE_MAX          (CFG_TUH_HUB ? 4 : 1)

// MIDI host string support (used by the vendored usb_midi_host driver)
#define CFG_MIDI_HOST_DEVSTRINGS    1

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
