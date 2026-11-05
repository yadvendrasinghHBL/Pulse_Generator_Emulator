/*
 * Line-oriented console on USART1 (FT231X USB-UART), 115200 8N1.
 * RX is interrupt driven into a ring buffer; TX is blocking.
 */
#ifndef CONSOLE_H
#define CONSOLE_H

#include <stddef.h>
#include <stdint.h>

/* Longest accepted command line, in characters */
#define CON_LINE_MAX      95U
/* con_getline() result: the line was longer than CON_LINE_MAX and was discarded */
#define CON_LINE_TOO_LONG (-1)

/* Receive problems since boot */
typedef struct {
  uint32_t rx_drops;   /* bytes lost because the RX ring buffer was full */
  uint32_t ore;        /* USART overrun: byte lost in hardware */
  uint32_t fe;         /* framing errors */
  uint32_t ne;         /* noise errors */
} con_stats_t;

void con_init(void);
void con_puts(const char *s);
void con_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* Non-blocking: returns 1 and fills buf (NUL-terminated) when a line is complete,
 * CON_LINE_TOO_LONG if the line was discarded, 0 otherwise */
int  con_getline(char *buf, size_t len);
void con_get_stats(con_stats_t *out);
/* 1 while the user is typing a line (or input is still unprocessed) */
int  con_line_pending(void);
void con_irq_handler(void);

#endif /* CONSOLE_H */
