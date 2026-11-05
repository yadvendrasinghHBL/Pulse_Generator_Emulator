#include "console.h"
#include "board.h"

#include <stdarg.h>
#include <stdio.h>

/* Power of two. Large enough that a host can send several commands without
 * waiting for the prompt: echo + replies are sent while more input arrives
 * (128 bytes lost 6 of 10 burst lines on board 1). */
#define RX_BUF_SIZE 1024U

static UART_HandleTypeDef huart;
static volatile uint8_t rx_buf[RX_BUF_SIZE];
static volatile uint32_t rx_head;
static uint32_t rx_tail;

/* Counted instead of silently cleared, so lost input is visible in `info` */
static volatile con_stats_t stats;

static char line[CON_LINE_MAX + 1];
static size_t line_len;
static int line_overflow;   /* input exceeded CON_LINE_MAX: reject, do not run */
static int last_was_cr;     /* a '\n' right after '\r' is the second half of CRLF */

void con_init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_USART1_CLK_ENABLE();

  gpio.Pin = CON_TX_PIN | CON_RX_PIN;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_PULLUP;   /* keep RX idle-high if the FT231X is unpowered */
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Alternate = CON_AF;
  HAL_GPIO_Init(CON_TX_PORT, &gpio);

  huart.Instance = CON_UART;
  huart.Init.BaudRate = CON_BAUD;
  huart.Init.WordLength = UART_WORDLENGTH_8B;
  huart.Init.StopBits = UART_STOPBITS_1;
  huart.Init.Parity = UART_PARITY_NONE;
  huart.Init.Mode = UART_MODE_TX_RX;
  huart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart.Init.OverSampling = UART_OVERSAMPLING_16;
  huart.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  HAL_UART_Init(&huart);

  __HAL_UART_ENABLE_IT(&huart, UART_IT_RXNE);
  HAL_NVIC_SetPriority(CON_UART_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(CON_UART_IRQn);
}

/* Called from USART1_IRQHandler. Reads RDR directly instead of using the HAL
 * receive state machine, so bytes are never dropped between re-arms. */
void con_irq_handler(void)
{
  uint32_t isr = CON_UART->ISR;

  if (isr & USART_ISR_RXNE) {
    uint8_t c = (uint8_t)CON_UART->RDR;
    uint32_t next = (rx_head + 1U) & (RX_BUF_SIZE - 1U);
    if (next != rx_tail) {
      rx_buf[rx_head] = c;
      rx_head = next;
    } else {
      stats.rx_drops++;
    }
  }
  if (isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) {
    if (isr & USART_ISR_ORE) {
      stats.ore++;
    }
    if (isr & USART_ISR_FE) {
      stats.fe++;
    }
    if (isr & USART_ISR_NE) {
      stats.ne++;
    }
    CON_UART->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
  }
}

int con_line_pending(void)
{
  return line_len > 0 || rx_tail != rx_head;
}

void con_get_stats(con_stats_t *out)
{
  /* Each field is a single aligned 32-bit read, so no IRQ masking is needed */
  out->rx_drops = stats.rx_drops;
  out->ore = stats.ore;
  out->fe = stats.fe;
  out->ne = stats.ne;
}

static void put_raw(const char *s, size_t n)
{
  HAL_UART_Transmit(&huart, (const uint8_t *)s, (uint16_t)n, 100);
}

void con_puts(const char *s)
{
  const char *start = s;

  for (; *s; s++) {
    if (*s == '\n') {
      put_raw(start, (size_t)(s - start));
      put_raw("\r\n", 2);
      start = s + 1;
    }
  }
  put_raw(start, (size_t)(s - start));
}

void con_printf(const char *fmt, ...)
{
  char buf[160];
  va_list ap;

  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  con_puts(buf);
}

int con_getline(char *buf, size_t len)
{
  while (rx_tail != rx_head) {
    char c = (char)rx_buf[rx_tail];
    rx_tail = (rx_tail + 1U) & (RX_BUF_SIZE - 1U);

    int crlf_tail = c == '\n' && last_was_cr;
    last_was_cr = c == '\r';
    if (c == '\r' || c == '\n') {
      /* Skip only the LF of a CRLF pair, so an empty Enter from a terminal
       * that sends LF alone still reaches the menu ("Enter = default") */
      if (crlf_tail) {
        continue;
      }
      put_raw("\r\n", 2);
      /* A cut-off command could run with the wrong arguments, so reject it */
      int too_long = line_overflow || line_len > len - 1;
      line_overflow = 0;
      if (too_long) {
        line_len = 0;
        return CON_LINE_TOO_LONG;
      }
      for (size_t i = 0; i < line_len; i++) {
        buf[i] = line[i];
      }
      buf[line_len] = '\0';
      line_len = 0;
      return 1;
    }
    if (c == '\b' || c == 0x7F) {
      if (line_len > 0) {
        line_len--;
        put_raw("\b \b", 3);
      }
      continue;
    }
    if (c >= ' ') {
      if (line_len < CON_LINE_MAX) {
        line[line_len++] = c;
        put_raw(&c, 1);
      } else {
        line_overflow = 1;
      }
    }
  }
  return 0;
}
