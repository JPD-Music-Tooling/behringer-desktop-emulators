/* SPDX-License-Identifier: AGPL-3.0-or-later
 * The AQ64L hardware for Behringer's demo firmware (0715-AAX, demo/Template/main.c and
 * demo/Demo/midi_demo.c, compiled unchanged), on the desktop:
 *
 *   - Userdrv/rgb.h: the 80 WS2812 LEDs. rgb_show() publishes the frame to the panel.
 *   - Userdrv/button.h: the 9x9 key matrix. The panel sets keys; button_get_state() reads them
 *     (the panel's keys do not bounce, so no debounce is needed).
 *   - Usbmidi/usb_midi.h: the USB MIDI class. Packets the firmware sends go to the panel, which
 *     sends them out on virtual MIDI ports; packets from the host are handed to the callback the
 *     firmware registered, from the firmware's own thread, between two main-loop passes, as the
 *     USB interrupt would between two instructions.
 *   - systick, FMC and reset. SysEx 0x77 writes the bootloader mailbox and resets: the emulator
 *     has no bootloader, so it stops the firmware and tells the panel ("bootloader").
 *
 * The firmware's main() is renamed aq64l_firmware_main at compile time and runs on the thread
 * that calls aq64l_run(). */
#include <pthread.h>
#include <setjmp.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "aq64l_emu.h"
#include "button.h"
#include "drv_usb_hw.h"
#include "gd32f3x0.h"
#include "main.h"
#include "midi_demo.h"
#include "rgb.h"
#include "systick.h"
#include "usb_midi.h"

int aq64l_firmware_main(void);

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&s_lock)
#define UNLOCK() pthread_mutex_unlock(&s_lock)

static aq64l_tx_fn s_tx;
static aq64l_event_fn s_event;
static void *s_ctx;

static jmp_buf s_exit;
static volatile int s_stop;

static void event(const char *what)
{
    if (s_event != NULL) {
        s_event(s_ctx, what);
    }
}

/* the firmware leaves its main loop only here: on a stop, and on a reset */
static void checkpoint(void)
{
    if (s_stop) {
        longjmp(s_exit, 1);
    }
}

static uint64_t now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000U + (uint64_t)t.tv_nsec / 1000U;
}

static uint64_t s_boot_us;

/* ---- RGB LEDs ------------------------------------------------------------------------------- */

static rgb_color_t s_buf[RGB_LED_COUNT];       /* the firmware's working buffer */
static uint8_t s_shown[RGB_LED_COUNT * 3];     /* the LEDs: the last frame sent out */
static uint8_t s_brightness = 128U;            /* as rgb.c */
static uint32_t s_frames;

void rgb_init(void)
{
    memset(s_buf, 0, sizeof(s_buf));
    rgb_show();
}

void rgb_set_color(uint16_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (index < RGB_LED_COUNT) {
        s_buf[index].r = r;
        s_buf[index].g = g;
        s_buf[index].b = b;
    }
}

void rgb_get_color(uint16_t index, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (index < RGB_LED_COUNT) {
        *r = s_buf[index].r;
        *g = s_buf[index].g;
        *b = s_buf[index].b;
    }
}

void rgb_set_color_struct(uint16_t index, rgb_color_t color)
{
    rgb_set_color(index, color.r, color.g, color.b);
}

void rgb_set_all(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t i;
    for (i = 0U; i < RGB_LED_COUNT; i++) {
        rgb_set_color(i, r, g, b);
    }
}

void rgb_clear(void)
{
    memset(s_buf, 0, sizeof(s_buf));
}

void rgb_set_brightness(uint8_t brightness)
{
    s_brightness = brightness;
}

void rgb_show(void)
{
    uint16_t i;
    LOCK();
    for (i = 0U; i < RGB_LED_COUNT; i++) {
        s_shown[i * 3U] = s_buf[i].r;
        s_shown[i * 3U + 1U] = s_buf[i].g;
        s_shown[i * 3U + 2U] = s_buf[i].b;
    }
    s_frames++;
    UNLOCK();
}

uint8_t rgb_tx_busy(void)
{
    return 0U;   /* a frame goes out at once */
}

/* ---- keys ----------------------------------------------------------------------------------- */

static uint8_t s_keys[BUTTON_MATRIX_BYTES];

void hc164_init(void) {}
void hc164_write(uint16_t value, uint8_t bits) { (void)value; (void)bits; }
void button_init(void) {}

void button_get_state(uint8_t *out_bits, uint16_t out_len)
{
    LOCK();
    memcpy(out_bits, s_keys, out_len < sizeof(s_keys) ? out_len : sizeof(s_keys));
    UNLOCK();
}

void button_matrix_scan(uint8_t *out_bits, uint16_t out_len)
{
    button_get_state(out_bits, out_len);
}

uint8_t button_get_pressed_key(void)
{
    uint8_t cur[BUTTON_MATRIX_BYTES], i;
    button_get_state(cur, sizeof(cur));
    for (i = 0U; i < BUTTON_MATRIX_KEYS; i++) {
        if (BUTTON_IS_PRESSED(cur, i)) {
            return i;
        }
    }
    return KEY_NONE;
}

uint16_t button_read_raw(void) { return 0x1FFU; }      /* active low: nothing */
uint16_t button_read_pressed(void) { return 0U; }

static usbd_midi_rx_cb s_rx_cb;
static uint8_t s_rx[1024];
static uint16_t s_rx_len;

/* once per main-loop pass: pace the loop (the matrix scan on the unit takes about 1 ms), and
 * deliver what the host sent meanwhile */
void button_scan(void)
{
    uint8_t pending[sizeof(s_rx)];
    uint16_t n;

    usleep(1000);
    checkpoint();
    LOCK();
    n = s_rx_len;
    memcpy(pending, s_rx, n);
    s_rx_len = 0U;
    UNLOCK();
    if ((n > 0U) && (s_rx_cb != NULL)) {
        s_rx_cb(pending, n);
    }
}

/* ---- time ----------------------------------------------------------------------------------- */

void systick_config(void) { s_boot_us = now_us(); }
void delay_decrement(void) {}
uint32_t get_ms(void) { return (uint32_t)((now_us() - s_boot_us) / 1000U); }

void delay_1ms(uint32_t count)
{
    while (count-- > 0U) {
        usleep(1000);
        checkpoint();
    }
}

/* ---- USB ------------------------------------------------------------------------------------ */

usb_desc midi_desc;
usb_class_core usbd_midi_cb;

void usb_rcu_config(void) {}
void usb_timer_init(void) {}
void usb_intr_config(void) {}
void usb_mdelay(uint32_t msec) { delay_1ms(msec); }
void usbd_disconnect(usb_core_driver *udev) { udev->configured = 0; }

/* the host configures the device right away: the class calls midi_demo_init on SET_CONFIGURATION */
void usbd_init(usb_core_driver *udev, usb_desc *desc, usb_class_core *class_core)
{
    (void)desc;
    (void)class_core;
    udev->configured = 1;
    midi_demo_init(udev);
}

void usbd_midi_register_rx_cb(usb_dev *udev, usbd_midi_rx_cb cb)
{
    (void)udev;
    s_rx_cb = cb;
}

uint8_t usbd_midi_send(usb_dev *udev, const uint8_t *buf, uint16_t len)
{
    (void)udev;
    if (s_tx != NULL) {
        s_tx(s_ctx, buf, len);
    }
    return 0U;   /* USBD_OK */
}

void midi_tx_kick(usb_dev *udev) { (void)udev; }

uint8_t usbd_hid_kbd_send(usb_dev *udev, const uint8_t report[HID_KBD_PACKET_SIZE])
{
    (void)udev;
    (void)report;
    return 0U;
}

/* ---- core, flash, reset --------------------------------------------------------------------- */

static uint16_t s_mailbox;

void __enable_irq(void) {}
void __disable_irq(void) {}
void fmc_unlock(void) {}
void fmc_lock(void) {}
void fmc_flag_clear(uint32_t flag) { (void)flag; }

int fmc_page_erase(uint32_t page_address)
{
    if (page_address == BOOT_FLAG_ADDR) {
        s_mailbox = 0xFFFFU;
    }
    return 0;
}

int fmc_halfword_program(uint32_t address, uint16_t data)
{
    if (address == BOOT_FLAG_ADDR) {
        s_mailbox = data;
    }
    return 0;
}

void NVIC_SystemReset(void)
{
    /* the factory bootloader would now see the mailbox and wait for an upload */
    event(s_mailbox == BOOT_FLAG_UPDATE ? "bootloader" : "reset");
    s_stop = 1;
    checkpoint();
}

/* ---- the panel's side ----------------------------------------------------------------------- */

void aq64l_set_callbacks(aq64l_tx_fn tx, aq64l_event_fn ev, void *ctx)
{
    s_tx = tx;
    s_event = ev;
    s_ctx = ctx;
}

void aq64l_set_key(int key, int down)
{
    if ((key < 0) || (key >= AQ64L_KEYS)) {
        return;
    }
    LOCK();
    if (down) {
        s_keys[key / 8] |= (uint8_t)(1U << (key % 8));
    } else {
        s_keys[key / 8] &= (uint8_t)~(1U << (key % 8));
    }
    UNLOCK();
}

int aq64l_key(int key)
{
    int on;
    if ((key < 0) || (key >= AQ64L_KEYS)) {
        return 0;
    }
    LOCK();
    on = (s_keys[key / 8] >> (key % 8)) & 1;
    UNLOCK();
    return on;
}

int aq64l_leds(uint8_t rgb[AQ64L_LEDS * 3], uint32_t *seq)
{
    LOCK();
    memcpy(rgb, s_shown, sizeof(s_shown));
    if (seq != NULL) {
        *seq = s_frames;
    }
    UNLOCK();
    return s_brightness;
}

void aq64l_host_packets(const uint8_t *packets, uint16_t len)
{
    LOCK();
    if ((uint32_t)s_rx_len + len <= sizeof(s_rx)) {   /* else dropped, as a full endpoint NAKs */
        memcpy(&s_rx[s_rx_len], packets, len);
        s_rx_len = (uint16_t)(s_rx_len + len);
    }
    UNLOCK();
}

void aq64l_run(void)
{
    s_stop = 0;
    if (setjmp(s_exit) == 0) {
        (void)aq64l_firmware_main();
    }
}

void aq64l_stop(void)
{
    s_stop = 1;
}
