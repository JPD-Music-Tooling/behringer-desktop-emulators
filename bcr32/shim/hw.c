/* SPDX-License-Identifier: AGPL-3.0-or-later
 * The BCR32 board for Behringer's demo firmware (0715-AAW, demo/App and ThirdParty/ssd1306,
 * compiled unchanged), on the desktop. It takes the place of demo/Board (the board support
 * package, written against the GigaDevice library) and emulates what is behind it:
 *
 *   - the 74HC165 chain (BspButtons_Read): 64 buttons, active low, and the 32 knob encoders as
 *     2-bit gray codes, in the register and bit order input.c decodes. A turn is played as
 *     quadrature steps, one per 1 ms scan, 4 per detent, so input.c's own decoder counts it;
 *   - the SELECT encoder's A/B lines on PA15/PA4 (gpio_input_bit_get);
 *   - five HT16K33 LED drivers (BspLed_*): their display RAM;
 *   - the SSD1306 OLED on SPI1 (BspSpi1_*, with D/C on PB12 and CS on PC12): a controller model
 *     that takes the command and data bytes the driver sends and keeps the 128x32 GDDRAM,
 *     including its addressing modes and segment/COM remap. The glass is taken as SEG0 at the
 *     left and COM0 at the top, the orientation in which the firmware's settings (A0, C0) read;
 *   - the W25X40CL SPI flash (BspFlash_*): 512 KB in memory, operations complete on the next poll;
 *   - CV DAC and gate outputs (BspCv_*, BspGate_Set): their values, for the panel;
 *   - DIN MIDI (BspMidiUart_*): bytes from and to the panel's virtual ports;
 *   - USB MIDI (usbd_midi.h): packets from and to the panel's virtual ports;
 *   - SysTick: a 1 kHz thread calls the firmware's SysTick_Handler;
 *   - SYNC: with the panel's cable plugged, SYNC IN follows SYNC OUT and edges raise the EXTI
 *     interrupts sync_loop.c counts.
 *
 * Not emulated: the internal flash self test (iflash_test.c reads the flash at its fixed address
 * and times it with the DWT counter; IFlashTest_* are empty here) and the bootloader
 * (BootJump_Start stops the firmware and tells the panel). */
#include <pthread.h>
#include <setjmp.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "bcr32_emu.h"
#include "boot_jump.h"
#include "bsp_buttons.h"
#include "bsp_cv.h"
#include "bsp_delay.h"
#include "bsp_flash.h"
#include "bsp_gpio.h"
#include "bsp_led.h"
#include "bsp_midi_uart.h"
#include "bsp_spi1.h"
#include "bsp_usb.h"
#include "gd32f4xx.h"
#include "iflash_test.h"
#include "usbd_midi.h"

int bcr32_firmware_main(void);
void SysTick_Handler(void);
void EXTI5_9_IRQHandler(void);
void EXTI10_15_IRQHandler(void);

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&s_lock)
#define UNLOCK() pthread_mutex_unlock(&s_lock)

static bcr32_usb_fn s_usb_tx;
static bcr32_din_fn s_din_tx;
static bcr32_event_fn s_event;
static void *s_ctx;

static jmp_buf s_exit;
static volatile int s_stop;
static bcr32_view s_view;

static void event(const char *what)
{
    if (s_event != NULL) {
        s_event(s_ctx, what);
    }
}

static void checkpoint(void)
{
    if (s_stop) {
        longjmp(s_exit, 1);
    }
}

static void changed(void) { s_view.seq++; }   /* with s_lock held */

/* ---- core ----------------------------------------------------------------------------------- */

uint32_t SystemCoreClock = 120000000U;

static pthread_t s_systick;
static volatile int s_systick_running;

static void *systick_thread(void *arg)
{
    struct timespec next;
    (void)arg;
    clock_gettime(CLOCK_MONOTONIC, &next);
    while (!s_stop) {
        next.tv_nsec += 1000000L;
        if (next.tv_nsec >= 1000000000L) {
            next.tv_nsec -= 1000000000L;
            next.tv_sec++;
        }
        while (1) {  /* sleep to the next millisecond (no clock_nanosleep on macOS) */
            struct timespec now;
            long long left;
            clock_gettime(CLOCK_MONOTONIC, &now);
            left = (long long)(next.tv_sec - now.tv_sec) * 1000000000LL + (next.tv_nsec - now.tv_nsec);
            if (left <= 0) {
                break;
            }
            usleep((useconds_t)(left / 1000 + 1));
        }
        SysTick_Handler();
    }
    s_systick_running = 0;
    return NULL;
}

uint32_t SysTick_Config(uint32_t ticks)
{
    (void)ticks;   /* SystemCoreClock / 1000: 1 ms */
    if (!s_systick_running) {
        s_systick_running = 1;
        pthread_create(&s_systick, NULL, systick_thread, NULL);
    }
    return 0U;
}

void __enable_irq(void) {}
void __disable_irq(void) {}
uint32_t __get_PRIMASK(void) { return 0U; }
void __set_PRIMASK(uint32_t mask) { (void)mask; }
void nvic_priority_group_set(uint32_t group) { (void)group; }
void nvic_irq_enable(uint8_t irq, uint8_t pre, uint8_t sub) { (void)irq; (void)pre; (void)sub; }
void rcu_periph_clock_enable(uint32_t periph) { (void)periph; }
void syscfg_exti_line_config(uint8_t port, uint8_t pin) { (void)port; (void)pin; }
void BspClock_Init(void) {}

void BspDelay_Cycles(uint32_t cycles) { (void)cycles; }

void BspDelay_Us(uint32_t us)
{
    if (us >= 100U) {
        usleep(us);
    }
    checkpoint();
}

/* ---- GPIO, SYNC ----------------------------------------------------------------------------- */

static uint16_t s_out[4];          /* output latch per port */
static volatile int s_cable;       /* SYNC OUT -> SYNC IN */
static uint32_t s_exti_flags;
static uint8_t s_sel = 0U;         /* SELECT encoder gray state 0..3 */

void BspGpio_Init(void)
{
    memset(s_out, 0, sizeof(s_out));
    s_out[GPIOC] |= (uint16_t)(GPIO_PIN_12 | GPIO_PIN_7);   /* OLED and flash deselected */
}

static void oled_reset(void);

static void pin_changed(uint32_t port, uint32_t pin, int level)
{
    if (port == GPIOA && pin == GPIO_PIN_8 && level == 0) {   /* OLED_RST */
        oled_reset();
    }
    if (s_cable && port == GPIOA && (pin == GPIO_PIN_0 || pin == GPIO_PIN_1)) {
        /* an edge on SYNC OUT reaches SYNC IN: the EXTI line of that input fires */
        if (pin == GPIO_PIN_0) {
            s_exti_flags |= EXTI_9;
            EXTI5_9_IRQHandler();
        } else {
            s_exti_flags |= EXTI_10;
            EXTI10_15_IRQHandler();
        }
    }
}

void gpio_bit_set(uint32_t port, uint32_t pin)
{
    const int was = (s_out[port] & pin) != 0U;
    s_out[port] |= (uint16_t)pin;
    if (!was) {
        pin_changed(port, pin, 1);
    }
}

void gpio_bit_reset(uint32_t port, uint32_t pin)
{
    const int was = (s_out[port] & pin) != 0U;
    s_out[port] &= (uint16_t)~pin;
    if (was) {
        pin_changed(port, pin, 0);
    }
}

FlagStatus gpio_input_bit_get(uint32_t port, uint32_t pin)
{
    /* SELECT encoder: input.c maps (A << 1) | B to gray states {2, 0, 3, 1} */
    static const uint8_t a_of[4] = {0U, 1U, 0U, 1U}, b_of[4] = {1U, 1U, 0U, 0U};
    if (port == GPIOA && pin == GPIO_PIN_15) {
        return a_of[s_sel] ? SET : RESET;
    }
    if (port == GPIOA && pin == GPIO_PIN_4) {
        return b_of[s_sel] ? SET : RESET;
    }
    if (port == GPIOA && pin == GPIO_PIN_9) {    /* SYNC IN tip */
        return (s_cable && (s_out[GPIOA] & GPIO_PIN_0)) ? SET : RESET;
    }
    if (port == GPIOA && pin == GPIO_PIN_10) {   /* SYNC IN ring */
        return (s_cable && (s_out[GPIOA] & GPIO_PIN_1)) ? SET : RESET;
    }
    return SET;   /* footswitches open (pulled up), nothing else is read */
}

void exti_init(uint32_t line, uint32_t mode, uint32_t trig) { (void)line; (void)mode; (void)trig; }
void exti_interrupt_flag_clear(uint32_t line) { s_exti_flags &= ~line; }
FlagStatus exti_interrupt_flag_get(uint32_t line) { return (s_exti_flags & line) ? SET : RESET; }

/* ---- buttons and encoders (74HC165 chain) --------------------------------------------------- */

static const uint8_t s_button_regs[8] = {0U, 3U, 6U, 9U, 12U, 13U, 14U, 15U};   /* as input.c */
static const uint8_t s_encoder_regs[8] = {1U, 2U, 4U, 5U, 7U, 8U, 10U, 11U};
static uint8_t s_pressed[BCR32_BUTTONS];          /* by button id - 1 */
static uint8_t s_gray[BCR32_ENCODERS];            /* knob gray states */
static int s_steps[BCR32_ENCODERS];               /* quadrature steps still to play, + = clockwise */

/* clockwise from each gray state (input.c: cw_prev = {1, 3, 0, 2}); counter clockwise is cw_prev */
static const uint8_t s_cw_next[4] = {2U, 0U, 3U, 1U};
static const uint8_t s_cw_prev[4] = {1U, 3U, 0U, 2U};

/* the button id input.c decodes from chain register d, bit r (0-based), as wired */
static uint32_t button_index(uint32_t d, uint32_t r)
{
    uint32_t id = ((7U - d) * 8U) + r;
    if ((id >= 4U) && (id < 8U)) {
        id += 4U;
    } else if ((id >= 8U) && (id < 12U)) {
        id -= 4U;
    }
    return id;
}

void BspButtons_Read(uint8_t regs[BSP_BUTTONS_NUM_SHIFT_REGS])
{
    uint32_t d, r, i;

    LOCK();
    for (i = 0U; i < BCR32_ENCODERS; i++) {   /* one quadrature step per scan */
        if (s_steps[i] > 0) {
            s_gray[i] = s_cw_next[s_gray[i]];
            s_steps[i]--;
        } else if (s_steps[i] < 0) {
            s_gray[i] = s_cw_prev[s_gray[i]];
            s_steps[i]++;
        }
    }
    s_sel = s_gray[BCR32_ENCODERS - 1];
    memset(regs, 0, BSP_BUTTONS_NUM_SHIFT_REGS);
    for (d = 0U; d < 8U; d++) {
        regs[s_button_regs[d]] = 0xFFU;   /* active low */
        for (r = 0U; r < 8U; r++) {
            if (s_pressed[button_index(d, r)]) {
                regs[s_button_regs[d]] &= (uint8_t)~(1U << r);
            }
        }
    }
    for (i = 0U; i < 8U; i++) {               /* knob 32 - (i * 4 + k) in register i, bits 2k */
        uint32_t k;
        for (k = 0U; k < 4U; k++) {
            regs[s_encoder_regs[i]] |= (uint8_t)(s_gray[31U - (i * 4U + k)] << (k * 2U));
        }
    }
    UNLOCK();
}

/* ---- LEDs (HT16K33) ------------------------------------------------------------------------- */

const uint8_t g_bsp_led_chips[BSP_LED_NUM_CHIPS] = {
    BSP_LED_CHIP_BTN, BSP_LED_CHIP_ENC_ROW1, BSP_LED_CHIP_ENC_ROW2, BSP_LED_CHIP_ENC_ROW3, BSP_LED_CHIP_ENC_ROW4
};

static int chip_index(uint8_t chip)
{
    int i;
    for (i = 0; i < (int)BSP_LED_NUM_CHIPS; i++) {
        if (g_bsp_led_chips[i] == chip) {
            return i;
        }
    }
    return -1;
}

void BspLedI2c_Init(void) {}

/* an I2C write to an HT16K33: a display RAM address and data, or a command */
int BspLedI2c_Write(uint8_t chip, const uint8_t *data, uint32_t len)
{
    const int c = chip_index(chip);
    uint32_t i;

    if ((c < 0) || (len == 0U)) {
        return -1;   /* no ACK */
    }
    LOCK();
    if (data[0] <= 0x0FU) {
        for (i = 1U; i < len; i++) {
            s_view.ram[c][(data[0] + i - 1U) & 0x0FU] = data[i];
        }
    } else if ((data[0] & 0xF0U) == 0xE0U) {
        s_view.dim[c] = data[0] & 0x0FU;
    } else if ((data[0] & 0xF8U) == 0x80U) {
        s_view.on[c] = data[0] & 0x01U;
    }
    changed();
    UNLOCK();
    return 0;
}

int BspLed_Write2(uint8_t chip, uint8_t first, uint8_t second)
{
    const uint8_t d[2] = {first, second};
    return BspLedI2c_Write(chip, d, 2U);
}

uint32_t BspLed_All(uint8_t on)
{
    uint32_t c, failed = 0U;
    uint8_t d[17];
    d[0] = 0x00U;
    memset(&d[1], on ? 0xFF : 0x00, 16U);
    for (c = 0U; c < BSP_LED_NUM_CHIPS; c++) {
        failed += (BspLedI2c_Write(g_bsp_led_chips[c], d, sizeof(d)) != 0) ? 1U : 0U;
    }
    return failed;
}

uint32_t BspLed_Init(uint8_t brightness)
{
    uint32_t c, failed = 0U;
    for (c = 0U; c < BSP_LED_NUM_CHIPS; c++) {
        const uint8_t osc = 0x21U, disp = 0x81U, dim = (uint8_t)(0xE0U | (brightness & 0x0FU));
        failed += (BspLedI2c_Write(g_bsp_led_chips[c], &osc, 1U) != 0) ? 1U : 0U;
        failed += (BspLedI2c_Write(g_bsp_led_chips[c], &dim, 1U) != 0) ? 1U : 0U;
        failed += (BspLedI2c_Write(g_bsp_led_chips[c], &disp, 1U) != 0) ? 1U : 0U;
    }
    return failed + BspLed_All(0U);
}

/* ---- SPI1 and the SSD1306 ------------------------------------------------------------------- */

static struct {
    uint8_t ram[8][128];
    uint8_t mode;                 /* 0 horizontal, 1 vertical, 2 page */
    uint8_t col, page, col0, col1, page0, page1;
    uint8_t seg_remap, com_remap, inverse, on, all_on;
    uint8_t mux, offset, start_line;
    uint8_t cmd[8], need, have;   /* a command waiting for its arguments */
} s_oled;

static void oled_reset(void)
{
    memset(&s_oled, 0, sizeof(s_oled));
    s_oled.mode = 2U;
    s_oled.col1 = 127U;
    s_oled.page1 = 7U;
    s_oled.mux = 63U;
}

static void oled_render(void)   /* with s_lock held */
{
    int x, y;
    for (y = 0; y < BCR32_OLED_H; y++) {
        const int com = s_oled.com_remap ? (int)s_oled.mux - y : y;
        const int row = (com + s_oled.offset + s_oled.start_line) & 63;
        for (x = 0; x < BCR32_OLED_W; x++) {
            const int col = s_oled.seg_remap ? 127 - x : x;
            int px = (s_oled.ram[row / 8][col] >> (row % 8)) & 1;
            if (s_oled.all_on) {
                px = 1;
            }
            s_view.oled[y][x] = (uint8_t)(px ^ s_oled.inverse);
        }
    }
    s_view.oled_on = s_oled.on;
    changed();
}

static uint8_t oled_args(uint8_t c)
{
    switch (c) {
    case 0x20: case 0x81: case 0xA8: case 0xD3: case 0xD5: case 0xD9: case 0xDA: case 0xDB: case 0x8D:
        return 1U;
    case 0x21: case 0x22: case 0xA3:
        return 2U;
    case 0x29: case 0x2A:
        return 5U;
    case 0x26: case 0x27:
        return 6U;
    default:
        return 0U;
    }
}

static void oled_command(const uint8_t *c)
{
    const uint8_t op = c[0];
    if (op <= 0x0F) {
        s_oled.col = (uint8_t)((s_oled.col & 0xF0U) | op);
    } else if (op <= 0x1F) {
        s_oled.col = (uint8_t)((s_oled.col & 0x0FU) | ((op & 0x07U) << 4));
    } else if (op == 0x20) {
        s_oled.mode = c[1] & 0x03U;
    } else if (op == 0x21) {
        s_oled.col0 = c[1] & 0x7FU;
        s_oled.col1 = c[2] & 0x7FU;
        s_oled.col = s_oled.col0;
    } else if (op == 0x22) {
        s_oled.page0 = c[1] & 0x07U;
        s_oled.page1 = c[2] & 0x07U;
        s_oled.page = s_oled.page0;
    } else if (op >= 0x40 && op <= 0x7F) {
        s_oled.start_line = op & 0x3FU;
    } else if (op == 0xA0 || op == 0xA1) {
        s_oled.seg_remap = op & 1U;
    } else if (op == 0xA4 || op == 0xA5) {
        s_oled.all_on = op & 1U;
    } else if (op == 0xA6 || op == 0xA7) {
        s_oled.inverse = op & 1U;
    } else if (op == 0xA8) {
        s_oled.mux = c[1] & 0x3FU;
    } else if (op == 0xAE || op == 0xAF) {
        s_oled.on = op & 1U;
    } else if (op >= 0xB0 && op <= 0xB7) {
        s_oled.page = op & 0x07U;
    } else if (op == 0xC0 || op == 0xC8) {
        s_oled.com_remap = (op == 0xC8) ? 1U : 0U;
    } else if (op == 0xD3) {
        s_oled.offset = c[1] & 0x3FU;
    }
    /* contrast, clocks, pre-charge, COM pins, VCOMH, charge pump, scrolling: no visible effect here */
}

static void oled_byte(uint8_t b, int data)
{
    if (!data) {
        if (s_oled.need == 0U) {
            s_oled.cmd[0] = b;
            s_oled.have = 1U;
            s_oled.need = oled_args(b);
        } else {
            s_oled.cmd[s_oled.have++] = b;
            s_oled.need--;
        }
        if (s_oled.need == 0U) {
            oled_command(s_oled.cmd);
        }
        return;
    }
    s_oled.ram[s_oled.page & 7U][s_oled.col & 127U] = b;
    if (s_oled.mode == 2U) {              /* page: the column advances, no wrap to the next page */
        if (s_oled.col < 127U) {
            s_oled.col++;
        }
    } else if (s_oled.mode == 0U) {       /* horizontal: column, then page, inside the window */
        if (s_oled.col >= s_oled.col1) {
            s_oled.col = s_oled.col0;
            s_oled.page = (s_oled.page >= s_oled.page1) ? s_oled.page0 : (uint8_t)(s_oled.page + 1U);
        } else {
            s_oled.col++;
        }
    } else {                              /* vertical: page, then column */
        if (s_oled.page >= s_oled.page1) {
            s_oled.page = s_oled.page0;
            s_oled.col = (s_oled.col >= s_oled.col1) ? s_oled.col0 : (uint8_t)(s_oled.col + 1U);
        } else {
            s_oled.page++;
        }
    }
}

static void (*s_spi_idle_hook)(void);

void BspSpi1_Init(void) {}
void BspSpi1_SetIdleHook(void (*hook)(void)) { s_spi_idle_hook = hook; }
uint8_t BspSpi1_DmaBusy(void) { return 0U; }
void BspSpi1_WaitIdle(void) {}

void BspSpi1_Write(const uint8_t *data, uint32_t len)
{
    uint32_t i;
    if ((s_out[GPIOC] & GPIO_PIN_12) != 0U) {   /* OLED not selected (the flash is emulated above SPI) */
        return;
    }
    LOCK();
    for (i = 0U; i < len; i++) {
        oled_byte(data[i], (s_out[GPIOB] & GPIO_PIN_12) != 0U);
    }
    oled_render();
    UNLOCK();
}

uint8_t BspSpi1_Transfer(uint8_t byte)
{
    BspSpi1_Write(&byte, 1U);
    return 0xFFU;
}

/* the transfer is done at once; then its completion and the bus-idle interrupt, as on the unit */
void BspSpi1_WriteDma(const uint8_t *data, uint32_t len, void (*done)(void))
{
    BspSpi1_Write(data, len);
    if (done != NULL) {
        done();
    }
    if (s_spi_idle_hook != NULL) {
        s_spi_idle_hook();
    }
}

void BspSpi1_ReadDma(uint8_t *data, uint32_t len, void (*done)(void))
{
    memset(data, 0xFF, len);
    if (done != NULL) {
        done();
    }
    if (s_spi_idle_hook != NULL) {
        s_spi_idle_hook();
    }
}

/* ---- SPI flash (W25X40CL) ------------------------------------------------------------------- */

static uint8_t s_flash[BSP_FLASH_SIZE];
static int s_flash_init;
static BspFlashDone_t s_flash_done;
static int s_flash_result;

static void flash_init(void)
{
    if (!s_flash_init) {
        memset(s_flash, 0xFF, sizeof(s_flash));
        s_flash_init = 1;
    }
}

uint16_t BspFlash_ReadId(void) { return 0xEF12U; }
uint8_t BspFlash_Busy(void) { return s_flash_done != NULL ? 1U : 0U; }

static int flash_start(BspFlashDone_t done, int result)
{
    if (s_flash_done != NULL) {
        return -1;   /* busy */
    }
    s_flash_done = done;
    s_flash_result = result;
    return 0;
}

static int writable(uint32_t addr, uint32_t len)
{
    return (addr >= BSP_FLASH_FIRST_FREE_SECTOR * BSP_FLASH_SECTOR_SIZE) && (addr + len <= BSP_FLASH_SIZE);
}

int BspFlash_Read(uint32_t addr, uint8_t *dst, uint32_t len, BspFlashDone_t done)
{
    flash_init();
    if (addr + len > BSP_FLASH_SIZE) {
        return -1;
    }
    if (BspFlash_Busy()) {
        return -1;
    }
    memcpy(dst, &s_flash[addr], len);
    return flash_start(done, 0);
}

int BspFlash_Program(uint32_t addr, const uint8_t *src, uint32_t len, BspFlashDone_t done)
{
    uint32_t i;
    flash_init();
    if (!writable(addr, len) || len > BSP_FLASH_PAGE_SIZE || BspFlash_Busy()) {
        return -1;
    }
    for (i = 0U; i < len; i++) {
        s_flash[addr + i] &= src[i];   /* programming only clears bits */
    }
    return flash_start(done, 0);
}

int BspFlash_EraseSector(uint32_t addr, BspFlashDone_t done)
{
    flash_init();
    addr -= addr % BSP_FLASH_SECTOR_SIZE;
    if (!writable(addr, BSP_FLASH_SECTOR_SIZE) || BspFlash_Busy()) {
        return -1;
    }
    memset(&s_flash[addr], 0xFF, BSP_FLASH_SECTOR_SIZE);
    return flash_start(done, 0);
}

void BspFlash_Poll(void)
{
    if (s_flash_done != NULL) {
        BspFlashDone_t done = s_flash_done;
        s_flash_done = NULL;
        done(s_flash_result);
    }
}

/* the internal flash test is not emulated (see the top of this file) */
void IFlashTest_Start(uint32_t now_ms) { (void)now_ms; }
void IFlashTest_Poll(uint32_t now_ms) { (void)now_ms; }

/* ---- CV and gates --------------------------------------------------------------------------- */

void BspCv_Init(void) {}

void BspCv_Write(uint32_t channel, uint16_t value)
{
    if (channel < 4U) {
        LOCK();
        const float v = (float)value * 10.0f / 65535.0f;
        if (s_view.cv[channel] != v) {
            s_view.cv[channel] = v;
            changed();
        }
        UNLOCK();
    }
}

void BspGate_Set(uint32_t gate, BspGateLevel_t level)
{
    static const uint8_t volts[3] = {0U, 5U, 10U};
    if (gate < 4U && (unsigned)level < 3U) {
        LOCK();
        s_view.gate[gate] = volts[level];
        changed();
        UNLOCK();
    }
}

/* ---- DIN MIDI ------------------------------------------------------------------------------- */

static uint8_t s_din_rx[BSP_MIDI_RX_BUFFER_SIZE];
static uint32_t s_din_rx_len, s_din_dropped;

void BspMidiUart_Init(void) {}
int BspMidiUart_IsTxBusy(BspMidiPort_t port) { (void)port; return 0; }
uint32_t BspMidiUart_GetRxDropped(void) { return s_din_dropped; }

int BspMidiUart_Send(BspMidiPort_t port, const uint8_t *data, uint32_t len)
{
    if (s_din_tx != NULL) {
        s_din_tx(s_ctx, (int)port, data, len);
    }
    return 0;
}

uint32_t BspMidiUart_Read(uint8_t *buf, uint32_t max)
{
    uint32_t n;
    LOCK();
    n = s_din_rx_len < max ? s_din_rx_len : max;
    memcpy(buf, s_din_rx, n);
    memmove(s_din_rx, &s_din_rx[n], s_din_rx_len - n);
    s_din_rx_len -= n;
    UNLOCK();
    return n;
}

/* ---- USB MIDI ------------------------------------------------------------------------------- */

static uint8_t s_usb_rx[1024];
static uint32_t s_usb_rx_len;

void BspUsb_Init(void) {}
uint8_t UsbMidiTxBusy(void) { return 0U; }
void UsbMidiTxBusyClear(void) {}

/* called once per main-loop pass: it also paces the loop, which on the unit spins freely */
uint8_t IsUsbConnected(void)
{
    usleep(100);
    checkpoint();
    return 1U;
}

uint32_t UsbMidiRxRead(uint8_t *dst, uint32_t max)
{
    uint32_t n;
    LOCK();
    n = s_usb_rx_len < max ? s_usb_rx_len : max;
    n -= n % 4U;   /* whole packets */
    memcpy(dst, s_usb_rx, n);
    memmove(s_usb_rx, &s_usb_rx[n], s_usb_rx_len - n);
    s_usb_rx_len -= n;
    UNLOCK();
    return n;
}

void USBTxNBytes(uint8_t *src, uint32_t nBytes)
{
    if (s_usb_tx != NULL) {
        s_usb_tx(s_ctx, src, nBytes);
    }
}

/* ---- bootloader ----------------------------------------------------------------------------- */

void BootJump_Start(void)
{
    event("bootloader");
    s_stop = 1;
    checkpoint();
}

/* ---- the panel's side ----------------------------------------------------------------------- */

void bcr32_set_callbacks(bcr32_usb_fn usb, bcr32_din_fn din, bcr32_event_fn ev, void *ctx)
{
    s_usb_tx = usb;
    s_din_tx = din;
    s_event = ev;
    s_ctx = ctx;
}

void bcr32_set_button(int id, int down)
{
    if (id >= 1 && id <= BCR32_BUTTONS) {
        LOCK();
        s_pressed[id - 1] = down ? 1U : 0U;
        UNLOCK();
    }
}

int bcr32_button(int id)
{
    int on = 0;
    if (id >= 1 && id <= BCR32_BUTTONS) {
        LOCK();
        on = s_pressed[id - 1];
        UNLOCK();
    }
    return on;
}

void bcr32_turn(int encoder, int detents)
{
    if (encoder >= 1 && encoder <= BCR32_ENCODERS) {
        LOCK();
        s_steps[encoder - 1] += detents * 4;
        UNLOCK();
    }
}

void bcr32_sync_cable(int plugged) { s_cable = plugged ? 1 : 0; }
int bcr32_sync_cable_plugged(void) { return s_cable; }

void bcr32_view_get(bcr32_view *out)
{
    LOCK();
    *out = s_view;
    UNLOCK();
}

void bcr32_usb_in(const uint8_t *packets, uint32_t len)
{
    LOCK();
    if (s_usb_rx_len + len <= sizeof(s_usb_rx)) {
        memcpy(&s_usb_rx[s_usb_rx_len], packets, len);
        s_usb_rx_len += len;
    }
    UNLOCK();
}

void bcr32_din_in(const uint8_t *bytes, uint32_t len)
{
    LOCK();
    if (s_din_rx_len + len <= sizeof(s_din_rx)) {
        memcpy(&s_din_rx[s_din_rx_len], bytes, len);
        s_din_rx_len += len;
    } else {
        s_din_dropped += len;
    }
    UNLOCK();
}

void bcr32_run(void)
{
    s_stop = 0;
    oled_reset();
    if (setjmp(s_exit) == 0) {
        (void)bcr32_firmware_main();
    }
}

void bcr32_stop(void)
{
    s_stop = 1;
}
