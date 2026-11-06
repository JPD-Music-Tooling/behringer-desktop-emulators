/* SPDX-License-Identifier: AGPL-3.0-or-later
 * The emulated BCR32 hardware, as the panel sees it. The firmware runs on its own thread
 * (bcr32_run); everything here may be called from any other thread. */
#ifndef BCR32_EMU_H
#define BCR32_EMU_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define BCR32_BUTTONS 64
#define BCR32_ENCODERS 33     /* knobs 1..32, 33 = SELECT */
#define BCR32_LED_CHIPS 5     /* HT16K33 at 0xE0 (keys), 0xE4, 0xE6, 0xE8, 0xEA (rings, rows 1..4) */
#define BCR32_OLED_W 128
#define BCR32_OLED_H 32

typedef void (*bcr32_usb_fn)(void *ctx, const uint8_t *packets, uint32_t len);
typedef void (*bcr32_din_fn)(void *ctx, int port, const uint8_t *bytes, uint32_t len);  /* port 0 = OUT A, 1 = OUT B */
typedef void (*bcr32_event_fn)(void *ctx, const char *what);

typedef struct {
    uint8_t ram[BCR32_LED_CHIPS][16];   /* each chip's display RAM */
    uint8_t on[BCR32_LED_CHIPS];        /* oscillator and display on */
    uint8_t dim[BCR32_LED_CHIPS];       /* brightness 0..15 */
    uint8_t oled[BCR32_OLED_H][BCR32_OLED_W];  /* what the glass shows, 0 or 1 */
    uint8_t oled_on;
    float cv[4];                        /* volts */
    uint8_t gate[4];                    /* 0, 5 or 10 V */
    uint32_t seq;                       /* changes with every update */
} bcr32_view;

void bcr32_set_callbacks(bcr32_usb_fn usb, bcr32_din_fn din, bcr32_event_fn event, void *ctx);
void bcr32_set_button(int id, int down);        /* id 1..64 as App/input.h */
int  bcr32_button(int id);
void bcr32_turn(int encoder, int detents);      /* 1..32 knobs, 33 SELECT; + is clockwise */
void bcr32_sync_cable(int plugged);             /* a cable from SYNC OUT to SYNC IN */
int  bcr32_sync_cable_plugged(void);
void bcr32_view_get(bcr32_view *out);
void bcr32_usb_in(const uint8_t *packets, uint32_t len);   /* USB-MIDI from the host */
void bcr32_din_in(const uint8_t *bytes, uint32_t len);     /* bytes into MIDI IN */
void bcr32_run(void);
void bcr32_stop(void);

#ifdef __cplusplus
}
#endif
#endif
