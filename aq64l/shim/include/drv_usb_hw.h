/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Stands in for the GigaDevice USB hardware layer (see usbd_core.h). */
#ifndef AQ64L_SHIM_DRV_USB_HW_H
#define AQ64L_SHIM_DRV_USB_HW_H
#include <stdint.h>
#include "usbd_core.h"

void usb_rcu_config(void);
void usb_timer_init(void);
void usb_intr_config(void);
void usb_mdelay(uint32_t msec);

#endif
