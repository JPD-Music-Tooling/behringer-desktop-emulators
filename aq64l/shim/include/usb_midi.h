/* SPDX-License-Identifier: AGPL-3.0-or-later
 * The interface of Behringer's Usbmidi/usb_midi.h that the AQ64L application uses, without the
 * descriptors and the GigaDevice types behind them. Cable 0 is the DAW port, cable 1 the user port. */
#ifndef AQ64L_SHIM_USB_MIDI_H
#define AQ64L_SHIM_USB_MIDI_H
#include <stdint.h>
#include "usbd_core.h"

#define USB_MIDI_PORT_DAW   0U
#define USB_MIDI_PORT_USER  1U
#define USB_MIDI_PORT_COUNT 2U
#define HID_KBD_PACKET_SIZE 8U

typedef void (*usbd_midi_rx_cb)(uint8_t *buf, uint16_t len);

extern usb_desc midi_desc;
extern usb_class_core usbd_midi_cb;

uint8_t usbd_midi_send(usb_dev *udev, const uint8_t *buf, uint16_t len);
void midi_tx_kick(usb_dev *udev);
void usbd_midi_register_rx_cb(usb_dev *udev, usbd_midi_rx_cb cb);
uint8_t usbd_hid_kbd_send(usb_dev *udev, const uint8_t report[HID_KBD_PACKET_SIZE]);

#endif
