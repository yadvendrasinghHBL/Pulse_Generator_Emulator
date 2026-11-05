/*
 * Diagnostics: chip identity, supply/temperature measurement, BOOT0 readback
 * and the power-on self-test. Proves the MCU side of the board works before
 * the pulse output is used. Taken from the Handoff Simulator bring-up firmware.
 */
#include "diag.h"
#include "board.h"
#include "console.h"

#include <stdio.h>
#include <string.h>

static ADC_HandleTypeDef hadc;

/* Kept for `status`: -1 until the first POST has run */
static int post_fails = -1;
static uint32_t reset_flags;

static const struct {
  uint32_t    flag;
  const char *name;
} reset_causes[] = {
  { RCC_CSR_LPWRRSTF, "low-power" }, { RCC_CSR_WWDGRSTF, "WWDG" },
  { RCC_CSR_IWDGRSTF, "IWDG" },      { RCC_CSR_SFTRSTF, "software" },
  { RCC_CSR_BORRSTF, "BOR/power-on" }, { RCC_CSR_PINRSTF, "NRST pin" },
  { RCC_CSR_OBLRSTF, "option-byte load" }, { RCC_CSR_FWRSTF, "firewall" },
};

static void adc_init(void)
{
  RCC_PeriphCLKInitTypeDef pclk = {0};

  pclk.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  pclk.AdcClockSelection = RCC_ADCCLKSOURCE_SYSCLK;
  HAL_RCCEx_PeriphCLKConfig(&pclk);
  __HAL_RCC_ADC_CLK_ENABLE();

  hadc.Instance = ADC1;
  hadc.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV4;   /* 80 MHz / 4 = 20 MHz */
  hadc.Init.Resolution = ADC_RESOLUTION_12B;
  hadc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc.Init.LowPowerAutoWait = DISABLE;
  hadc.Init.ContinuousConvMode = DISABLE;
  hadc.Init.NbrOfConversion = 1;
  hadc.Init.DiscontinuousConvMode = DISABLE;
  hadc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc.Init.DMAContinuousRequests = DISABLE;
  hadc.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
  hadc.Init.OversamplingMode = DISABLE;
  HAL_ADC_Init(&hadc);
  HAL_ADCEx_Calibration_Start(&hadc, ADC_SINGLE_ENDED);
}

void diag_init(void)
{
  reset_flags = RCC->CSR;
  __HAL_RCC_CLEAR_RESET_FLAGS();
  adc_init();
}

static int adc_read(uint32_t channel, uint32_t *raw)
{
  ADC_ChannelConfTypeDef ch = {0};

  ch.Channel = channel;
  ch.Rank = ADC_REGULAR_RANK_1;
  ch.SamplingTime = ADC_SAMPLETIME_247CYCLES_5;   /* temp sensor needs >= 5 us */
  ch.SingleDiff = ADC_SINGLE_ENDED;
  ch.OffsetNumber = ADC_OFFSET_NONE;
  if (HAL_ADC_ConfigChannel(&hadc, &ch) != HAL_OK) {
    return -1;
  }
  if (HAL_ADC_Start(&hadc) != HAL_OK) {
    return -1;
  }
  int ok = HAL_ADC_PollForConversion(&hadc, 10) == HAL_OK;
  *raw = HAL_ADC_GetValue(&hadc);
  HAL_ADC_Stop(&hadc);
  return ok ? 0 : -1;
}

/* VDDA from the factory VREFINT calibration; VDDA = Vdrive through ferrite L1 */
int diag_measure(uint32_t *vdda_mv, int32_t *temp_c)
{
  uint32_t vref_raw, ts_raw;

  if (adc_read(ADC_CHANNEL_VREFINT, &vref_raw) || vref_raw == 0) {
    return -1;
  }
  *vdda_mv = __LL_ADC_CALC_VREFANALOG_VOLTAGE(vref_raw, LL_ADC_RESOLUTION_12B);
  if (adc_read(ADC_CHANNEL_TEMPSENSOR, &ts_raw)) {
    return -1;
  }
  *temp_c = __LL_ADC_CALC_TEMPERATURE(*vdda_mv, ts_raw, LL_ADC_RESOLUTION_12B);
  return 0;
}

void diag_print_info(void)
{
  uint32_t optr = FLASH->OPTR;

  con_printf("MCU      : DEV_ID 0x%03lX (expect 0x%03X STM32L45x), REV_ID 0x%04lX\n",
             HAL_GetDEVID(), BOARD_EXPECTED_DEV_ID, HAL_GetREVID());
  con_printf("Flash    : %u KB (expect %u)\n",
             *(const uint16_t *)FLASHSIZE_BASE, BOARD_EXPECTED_FLASH_KB);
  con_printf("UID      : %08lX-%08lX-%08lX\n", HAL_GetUIDw2(), HAL_GetUIDw1(), HAL_GetUIDw0());
  con_printf("SYSCLK   : %lu Hz (HSI16 -> PLL)\n", HAL_RCC_GetSysClockFreq());
  con_printf("HAL      : 0x%08lX\n", HAL_GetHalVersion());
  con_printf("Option   : RDP 0x%02lX nBOOT0 %lu nSWBOOT0 %lu nBOOT1 %lu\n",
             optr & FLASH_OPTR_RDP,
             (optr & FLASH_OPTR_nBOOT0) ? 1UL : 0UL,
             (optr & FLASH_OPTR_nSWBOOT0) ? 1UL : 0UL,
             (optr & FLASH_OPTR_nBOOT1) ? 1UL : 0UL);
  con_puts("Reset    :");
  for (size_t i = 0; i < sizeof(reset_causes) / sizeof(reset_causes[0]); i++) {
    if (reset_flags & reset_causes[i].flag) {
      con_printf(" %s", reset_causes[i].name);
    }
  }
  con_printf("\nBOOT0    : pin reads %d now\n", diag_boot0_level());

  con_stats_t st;
  con_get_stats(&st);
  con_printf("UART RX  : drops %lu, overrun %lu, framing %lu, noise %lu\n",
             st.rx_drops, st.ore, st.fe, st.ne);
}

static void check(int ok, const char *what, int *fails)
{
  con_printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) {
    (*fails)++;
  }
}

int diag_post(void)
{
  int fails = 0;
  char msg[80];
  uint32_t vdda = 0;
  int32_t temp = 0;

  con_puts("POST:\n");

  uint32_t dev = HAL_GetDEVID();
  snprintf(msg, sizeof(msg), "device ID 0x%03lX", dev);
  check(dev == BOARD_EXPECTED_DEV_ID, msg, &fails);

  uint16_t kb = *(const uint16_t *)FLASHSIZE_BASE;
  snprintf(msg, sizeof(msg), "flash size %u KB", kb);
  check(kb == BOARD_EXPECTED_FLASH_KB, msg, &fails);

  uint32_t sysclk = HAL_RCC_GetSysClockFreq();
  snprintf(msg, sizeof(msg), "SYSCLK %lu Hz from PLL", sysclk);
  check(sysclk == 80000000U && __HAL_RCC_GET_SYSCLK_SOURCE() == RCC_SYSCLKSOURCE_STATUS_PLLCLK,
        msg, &fails);

  if (diag_measure(&vdda, &temp) == 0) {
    snprintf(msg, sizeof(msg), "Vdrive (VDDA) %lu mV, %u..%u", vdda, BOARD_VDD_MIN_MV, BOARD_VDD_MAX_MV);
    check(vdda >= BOARD_VDD_MIN_MV && vdda <= BOARD_VDD_MAX_MV, msg, &fails);
    snprintf(msg, sizeof(msg), "die temperature %ld C", temp);
    check(temp > -20 && temp < 85, msg, &fails);
  } else {
    check(0, "ADC conversion", &fails);
  }

  con_printf("POST %s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails, fails == 1 ? "" : "s");
  post_fails = fails;
  return fails;
}

int diag_post_result(void)
{
  return post_fails;
}

/* ---- BOOT0 ---------------------------------------------------------------- */

/* PH3/BOOT0 is readable as a GPIO after boot. Shows what the FT231X RTS# line
 * is doing to BOOT0 right now (see docs/HARDWARE.md, auto-boot circuit). */
int diag_boot0_level(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOH_CLK_ENABLE();
  gpio.Pin = BOOT0_PIN;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_NOPULL;   /* R2 10k pull-down is on the board */
  HAL_GPIO_Init(BOOT0_PORT, &gpio);
  return HAL_GPIO_ReadPin(BOOT0_PORT, BOOT0_PIN) == GPIO_PIN_SET;
}
