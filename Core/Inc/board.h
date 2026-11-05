/*
 * Board definition: Handoff Simulator rev A hardware (STM32L451RCT6, LQFP64),
 * used here as a pulse generator emulator through expansion header J8.
 *
 * Every assignment below was taken from the KiCad netlist of
 * Handoff_Simulator/hardware/1_Handoff_Simulator/DesignFiles/Handoff_Simulator.kicad_pro
 * (see docs/HARDWARE.md). Do not add a pin here without checking it against
 * the schematic.
 */
#ifndef BOARD_H
#define BOARD_H

#include "stm32l4xx_hal.h"

/* No HSE/LSE crystal is fitted (PH0/PH1/PC14/PC15 unconnected): HSI16 only. */

/* LEDs, active high through 470R to GND */
#define LED_GREEN_PORT      GPIOC   /* D5 via R4, U1 pin 51 */
#define LED_GREEN_PIN       GPIO_PIN_10
#define LED_ORANGE_PORT     GPIOC   /* D3 via R3, U1 pin 53 */
#define LED_ORANGE_PIN      GPIO_PIN_12

/* USART1 <-> FT231X (U4) USB-UART, also the ROM bootloader port */
#define CON_UART            USART1
#define CON_UART_IRQn       USART1_IRQn
#define CON_TX_PORT         GPIOA   /* PA9  -> U4.RXD (pin 4)  */
#define CON_TX_PIN          GPIO_PIN_9
#define CON_RX_PORT         GPIOA   /* PA10 <- U4.TXD (pin 20) */
#define CON_RX_PIN          GPIO_PIN_10
#define CON_AF              GPIO_AF7_USART1
#define CON_BAUD            115200U

/* Pulse output 1: J8.1 = PA5 (U1 pin 21), TIM2_CH1 on AF1. TIM2 is 32-bit. */
#define PULSE1_PORT         GPIOA
#define PULSE1_PIN          GPIO_PIN_5
#define PULSE1_AF           GPIO_AF1_TIM2
#define PULSE1_NAME         "J8.1 (PA5, TIM2_CH1)"

/*
 * Sensor outputs A/B (stage 2): J8.6 = PB0 (U1 pin 26), TIM1_CH2N on AF1;
 * J8.7 = PB1 (U1 pin 27), TIM1_CH3N on AF1 (STM32CubeMX pin database,
 * GPIO-STM32L45x). One timer for both keeps them exactly 90 degrees apart.
 */
#define QUAD_PORT           GPIOB
#define QUAD_A_PIN          GPIO_PIN_0
#define QUAD_B_PIN          GPIO_PIN_1
#define QUAD_AF             GPIO_AF1_TIM1
#define QUAD_NAME           "A J8.6 (PB0) / B J8.7 (PB1), TIM1"

/*
 * Receiver inputs (stage 4): J8.2 = PA6 (U1 pin 22), TIM3_CH1 on AF2;
 * J8.3 = PA7 (U1 pin 23), TIM3_CH2 on AF2. 3.3 V logic only.
 */
#define RX_A_PORT           GPIOA
#define RX_A_PIN            GPIO_PIN_6
#define RX_B_PORT           GPIOA
#define RX_B_PIN            GPIO_PIN_7
#define RX_AF               GPIO_AF2_TIM3
#define RX_NAME             "A J8.2 (PA6) / B J8.3 (PA7), TIM3"

/*
 * Attenuator latch: the eight QPC6713 attenuators of the Handoff Simulator are
 * still on the board. This firmware does not use them, but their LE line (PB6,
 * U1 pin 58) has no pull-down and must not float (docs/HARDWARE.md finding 4).
 * It is driven low at boot and kept low; the attenuators stay at their
 * power-up value (31.75 dB per stage).
 */
#define ATT_LE_PORT         GPIOB
#define ATT_LE_PIN          GPIO_PIN_6

/* BOOT0 (PH3/BOOT0, pin 60): 10k pull-down R2, driven by FT231X RTS# via D2 */
#define BOOT0_PORT          GPIOH
#define BOOT0_PIN           GPIO_PIN_3

/* Expected silicon (DBGMCU_IDCODE.DEV_ID, flash size register) */
#define BOARD_EXPECTED_DEV_ID     0x462U  /* STM32L45x/L46x */
#define BOARD_EXPECTED_FLASH_KB   256U    /* STM32L451xC   */

/* Vdrive is the 3.3 V LDO40LPU33 (U3) output */
#define BOARD_VDD_MIN_MV          3200U
#define BOARD_VDD_MAX_MV          3400U

void board_led(GPIO_TypeDef *port, uint16_t pin, int on);
/* Drive the attenuator LE line low (called first in main and before the bootloader jump) */
void board_hold_att_latch(void);

#endif /* BOARD_H */
