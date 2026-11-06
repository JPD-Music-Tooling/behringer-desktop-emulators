/* SPDX-License-Identifier: AGPL-3.0-or-later
 * The interface of the BCR32's USB MIDI class (UsbDriver/midi/inc/usbd_midi.h) the application
 * uses. The emulator has no USB stack: packets go to and come from virtual MIDI ports. */
#ifndef BCR32_SHIM_USBD_MIDI_H
#define BCR32_SHIM_USBD_MIDI_H
#include <stdint.h>

void USBTxNBytes(uint8_t *src, uint32_t nBytes);
uint8_t UsbMidiTxBusy(void);
void UsbMidiTxBusyClear(void);
uint32_t UsbMidiRxRead(uint8_t *dst, uint32_t max);
uint8_t IsUsbConnected(void);

#endif
