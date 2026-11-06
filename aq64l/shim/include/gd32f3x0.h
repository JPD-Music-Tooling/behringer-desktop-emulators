/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Stands in for GigaDevice's gd32f3x0.h when Behringer's AQ64L demo firmware is built for the
 * desktop: only what the application code (Template/main.c, Demo/midi_demo.c and the Userdrv
 * headers) names. Hardware access lands in ../hw.c. */
#ifndef AQ64L_SHIM_GD32F3X0_H
#define AQ64L_SHIM_GD32F3X0_H
#include <stdint.h>

#define __IO volatile

#define GPIOA 0U
#define GPIOB 1U
#define GPIOC 2U
#define RCU_GPIOB 1U
#define RCU_GPIOC 2U
#define GPIO_PIN_0 (1U << 0)
#define GPIO_PIN_1 (1U << 1)
#define GPIO_PIN_2 (1U << 2)
#define GPIO_PIN_3 (1U << 3)
#define GPIO_PIN_4 (1U << 4)
#define GPIO_PIN_5 (1U << 5)
#define GPIO_PIN_6 (1U << 6)
#define GPIO_PIN_7 (1U << 7)
#define GPIO_PIN_8 (1U << 8)

#define FMC_FLAG_END   (1U << 5)
#define FMC_FLAG_WPERR (1U << 4)
#define FMC_FLAG_PGERR (1U << 2)

void __enable_irq(void);
void __disable_irq(void);
void fmc_unlock(void);
void fmc_lock(void);
void fmc_flag_clear(uint32_t flag);
int  fmc_page_erase(uint32_t page_address);
int  fmc_halfword_program(uint32_t address, uint16_t data);
void NVIC_SystemReset(void);

#endif
