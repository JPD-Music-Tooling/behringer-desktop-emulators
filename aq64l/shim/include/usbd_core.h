/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Stands in for the GigaDevice USB device core: the emulator has no USB stack. The firmware's USB
 * MIDI endpoints become virtual MIDI ports on the computer (see ../hw.c and ../../panel). */
#ifndef AQ64L_SHIM_USBD_CORE_H
#define AQ64L_SHIM_USBD_CORE_H
#include <stdint.h>

typedef struct usb_core_driver { int configured; } usb_core_driver;
typedef usb_core_driver usb_dev;
typedef struct usb_desc { int unused; } usb_desc;
typedef struct usb_class_core { int unused; } usb_class_core;

void usbd_init(usb_core_driver *udev, usb_desc *desc, usb_class_core *class_core);
void usbd_disconnect(usb_core_driver *udev);

#endif
