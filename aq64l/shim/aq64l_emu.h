/* SPDX-License-Identifier: AGPL-3.0-or-later
 * The emulated AQ64L hardware, as the panel sees it. The firmware runs on its own thread
 * (aq64l_run); everything here may be called from any other thread. */
#ifndef AQ64L_EMU_H
#define AQ64L_EMU_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define AQ64L_KEYS 81
#define AQ64L_LEDS 80

typedef void (*aq64l_tx_fn)(void *ctx, const uint8_t *packets, uint16_t len);
typedef void (*aq64l_event_fn)(void *ctx, const char *what);

void aq64l_set_callbacks(aq64l_tx_fn tx, aq64l_event_fn event, void *ctx);
void aq64l_set_key(int key, int down);          /* key index 0..80 as Userdrv/button.h */
int  aq64l_key(int key);
/* the LED colours the firmware last sent out (rgb_show): r,g,b per LED; returns the brightness
 * scale the firmware applies (0..255); *seq counts frames */
int  aq64l_leds(uint8_t rgb[AQ64L_LEDS * 3], uint32_t *seq);
void aq64l_host_packets(const uint8_t *packets, uint16_t len);  /* USB-MIDI from the host */
void aq64l_run(void);                           /* the firmware's main(); returns after aq64l_stop */
void aq64l_stop(void);

#ifdef __cplusplus
}
#endif
#endif
