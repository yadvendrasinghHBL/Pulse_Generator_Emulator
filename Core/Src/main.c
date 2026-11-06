/*
 * Pulse Generator Emulator firmware (Handoff Simulator rev A hardware).
 *
 * Emulates an axle-mounted optical pulse generator: two 50 % square waves
 * 90 degrees apart on J8.6/J8.7 (TIM1) plus a single output on J8.1 (PA5,
 * TIM2_CH1), all set over the USB serial console. A receiver on J8.2/J8.3
 * (TIM3 input capture) measures A/B signals and shows the speed parameters.
 *
 * Boot sequence: attenuator LE low -> clocks -> LEDs -> console -> POST ->
 * pulse timers -> saved settings -> command shell (outputs stay off until
 * `start` or `speed`).
 * Green LED = heartbeat (firmware alive), orange LED = POST failure / fault.
 */
#include "board.h"
#include "cli.h"
#include "console.h"
#include "diag.h"
#include "pulse.h"
#include "quad.h"
#include "rx.h"
#include "settings.h"
#include "speed.h"
#include "version.h"

/* Pause before the banner so the boot output can be watched on a terminal */
#define BANNER_DELAY_MS 2000U

void Error_Handler(void);

/* HSI16 -> PLL (M=1, N=10, R=2) -> 80 MHz SYSCLK, voltage range 1, 4 WS.
 * APB1 = APB2 = 80 MHz, so the TIM2 and TIM1 clocks are 80 MHz (12.5 ns). */
static void clock_init(void)
{
  RCC_OscInitTypeDef osc = {0};
  RCC_ClkInitTypeDef clk = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK) {
    Error_Handler();
  }

  osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  osc.HSIState = RCC_HSI_ON;
  osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  osc.PLL.PLLState = RCC_PLL_ON;
  osc.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  osc.PLL.PLLM = 1;
  osc.PLL.PLLN = 10;
  osc.PLL.PLLP = RCC_PLLP_DIV7;
  osc.PLL.PLLQ = RCC_PLLQ_DIV2;
  osc.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
    Error_Handler();
  }

  clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clk.APB1CLKDivider = RCC_HCLK_DIV1;
  clk.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4) != HAL_OK) {
    Error_Handler();
  }
}

static void led_init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  HAL_GPIO_WritePin(GPIOC, LED_GREEN_PIN | LED_ORANGE_PIN, GPIO_PIN_RESET);
  gpio.Pin = LED_GREEN_PIN | LED_ORANGE_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &gpio);
}

void board_led(GPIO_TypeDef *port, uint16_t pin, int on)
{
  HAL_GPIO_WritePin(port, pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void board_hold_att_latch(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOB_CLK_ENABLE();
  HAL_GPIO_WritePin(ATT_LE_PORT, ATT_LE_PIN, GPIO_PIN_RESET);
  gpio.Pin = ATT_LE_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(ATT_LE_PORT, &gpio);
}

int main(void)
{
  char line[CON_LINE_MAX + 1];

  HAL_Init();
  /* First thing: the unused attenuators' LE line must not float */
  board_hold_att_latch();
  clock_init();
  led_init();

  /* Lamp test: both LEDs on briefly */
  board_led(LED_GREEN_PORT, LED_GREEN_PIN, 1);
  board_led(LED_ORANGE_PORT, LED_ORANGE_PIN, 1);
  HAL_Delay(300);
  board_led(LED_ORANGE_PORT, LED_ORANGE_PIN, 0);

  con_init();
  diag_init();
  pulse_init();
  quad_init();
  settings_init();               /* after quad_init: applies the saved direction */
  rx_init();

  HAL_Delay(BANNER_DELAY_MS);
  con_printf("\n\nPulse Generator Emulator v%s (%s %s)\n", FW_VERSION, __DATE__, __TIME__);
  diag_print_info();
  int fails = diag_post();
  board_led(LED_ORANGE_PORT, LED_ORANGE_PIN, fails != 0);
  con_printf("Settings %s: %lu ppr, wheel %lu mm, accel %lu.%03lu m/s2, dir %s\n",
             settings_from_flash() ? "from flash" : "defaults (nothing saved)",
             settings_ppr(), settings_wheel_mm(), settings_accel_mm_s2() / 1000U,
             settings_accel_mm_s2() % 1000U, quad_dir() == QUAD_FWD ? "fwd" : "rev");
  con_printf("Outputs %s and %s: off. Type 'start' to begin, 'help' for commands.\n",
             PULSE1_NAME, QUAD_NAME);
  cli_prompt();

  uint32_t last_beat = HAL_GetTick();
  for (;;) {
    int got = con_getline(line, sizeof(line));
    if (got > 0) {
      cli_execute(line);
    } else if (got == CON_LINE_TOO_LONG) {
      con_printf("ERR line too long (max %u chars)\n", CON_LINE_MAX);
      cli_prompt();
    }
    rx_poll();
    speed_poll();
    if (HAL_GetTick() - last_beat >= 500U) {
      last_beat += 500U;
      HAL_GPIO_TogglePin(LED_GREEN_PORT, LED_GREEN_PIN);
    }
  }
}

/* Unrecoverable init error: fast orange blink, independent of SysTick/clocks */
void Error_Handler(void)
{
  __disable_irq();
  TIM2->CR1 &= ~TIM_CR1_CEN;
  TIM1->CR1 &= ~TIM_CR1_CEN;
  led_init();
  for (;;) {
    HAL_GPIO_TogglePin(LED_ORANGE_PORT, LED_ORANGE_PIN);
    for (volatile uint32_t i = 0; i < 100000U; i++) {
    }
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  Error_Handler();
}
#endif
