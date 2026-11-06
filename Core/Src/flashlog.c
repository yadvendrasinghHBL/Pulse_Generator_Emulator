/*
 * Flash record log (see flashlog.h): erase / program / CRC for one page.
 * Only called at standstill (the callers refuse while moving or receiving),
 * because the CPU and all flash-resident interrupts stall during an erase.
 */
#include "flashlog.h"
#include "board.h"

uint32_t flashlog_crc32(const void *data, size_t len)
{
  const uint8_t *p = data;
  uint32_t crc = 0xFFFFFFFFU;

  while (len--) {
    crc ^= *p++;
    for (int k = 0; k < 8; k++) {
      crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
  }
  return ~crc;
}

int flashlog_erased(uint32_t addr, size_t len)
{
  const volatile uint32_t *w = (const volatile uint32_t *)addr;

  for (size_t k = 0; k < len / 4U; k++) {
    if (w[k] != 0xFFFFFFFFU) {
      return 0;
    }
  }
  return 1;
}

uint32_t flashlog_used(uint32_t page, size_t rec_len)
{
  uint32_t slots = FLASHLOG_PAGE_BYTES / rec_len;
  uint32_t i = 0;

  while (i < slots && !flashlog_erased(page + i * rec_len, rec_len)) {
    i++;
  }
  return i;
}

int flashlog_append(uint32_t page, size_t rec_len, uint32_t *used, const void *rec)
{
  uint32_t slots = FLASHLOG_PAGE_BYTES / rec_len;
  uint32_t i = *used;
  const uint8_t *src = rec;
  int err = 0;

  HAL_FLASH_Unlock();
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS | FLASH_FLAG_OPTVERR);
  if (i >= slots || !flashlog_erased(page + i * rec_len, rec_len)) {
    FLASH_EraseInitTypeDef e = {0};
    uint32_t page_err = 0;

    e.TypeErase = FLASH_TYPEERASE_PAGES;
    e.Banks = FLASH_BANK_1;
    e.Page = (page - FLASH_BASE) / FLASH_PAGE_SIZE;
    e.NbPages = 1;
    err = HAL_FLASHEx_Erase(&e, &page_err) != HAL_OK;
    i = 0;
  }
  for (size_t k = 0; !err && k < rec_len / 8U; k++) {
    uint64_t dw = 0;

    for (int b = 7; b >= 0; b--) {
      dw = (dw << 8) | src[8U * k + (size_t)b];   /* little-endian, any alignment */
    }
    err = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, page + i * rec_len + 8U * k, dw) != HAL_OK;
  }
  HAL_FLASH_Lock();
  if (err) {
    *used = slots;                /* force an erase on the next try */
    return -1;
  }
  *used = i + 1U;
  return (int)i;
}
