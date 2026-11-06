/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Stands in for GigaDevice's gd32f4xx.h when Behringer's BCR32 demo firmware is built for the
 * desktop: only what the application code (App/, ThirdParty/ssd1306, the Board headers) names.
 * The hardware behind it is in ../hw.c. */
#ifndef BCR32_SHIM_GD32F4XX_H
#define BCR32_SHIM_GD32F4XX_H
#include <stdint.h>

typedef enum { RESET = 0, SET = !RESET } FlagStatus;

#define GPIOA 0U
#define GPIOB 1U
#define GPIOC 2U
#define GPIOD 3U
#define GPIO_PIN_0  (1U << 0)
#define GPIO_PIN_1  (1U << 1)
#define GPIO_PIN_2  (1U << 2)
#define GPIO_PIN_3  (1U << 3)
#define GPIO_PIN_4  (1U << 4)
#define GPIO_PIN_5  (1U << 5)
#define GPIO_PIN_6  (1U << 6)
#define GPIO_PIN_7  (1U << 7)
#define GPIO_PIN_8  (1U << 8)
#define GPIO_PIN_9  (1U << 9)
#define GPIO_PIN_10 (1U << 10)
#define GPIO_PIN_11 (1U << 11)
#define GPIO_PIN_12 (1U << 12)
#define GPIO_PIN_13 (1U << 13)
#define GPIO_PIN_14 (1U << 14)
#define GPIO_PIN_15 (1U << 15)

#define RCU_SYSCFG 0U
#define EXTI_SOURCE_GPIOA 0U
#define EXTI_SOURCE_PIN9 9U
#define EXTI_SOURCE_PIN10 10U
#define EXTI_9  (1U << 9)
#define EXTI_10 (1U << 10)
#define EXTI_INTERRUPT 0U
#define EXTI_TRIG_BOTH 2U
#define EXTI5_9_IRQn 23
#define EXTI10_15_IRQn 40
#define NVIC_PRIGROUP_PRE4_SUB0 0x300U

extern uint32_t SystemCoreClock;

void gpio_bit_set(uint32_t port, uint32_t pin);
void gpio_bit_reset(uint32_t port, uint32_t pin);
FlagStatus gpio_input_bit_get(uint32_t port, uint32_t pin);
void rcu_periph_clock_enable(uint32_t periph);
void syscfg_exti_line_config(uint8_t port, uint8_t pin);
void exti_init(uint32_t line, uint32_t mode, uint32_t trig);
void exti_interrupt_flag_clear(uint32_t line);
FlagStatus exti_interrupt_flag_get(uint32_t line);
void nvic_irq_enable(uint8_t irq, uint8_t pre, uint8_t sub);
void nvic_priority_group_set(uint32_t group);
uint32_t SysTick_Config(uint32_t ticks);
void __enable_irq(void);
void __disable_irq(void);
uint32_t __get_PRIMASK(void);
void __set_PRIMASK(uint32_t mask);

#endif
