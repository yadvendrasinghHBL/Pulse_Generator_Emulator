/* Cortex-M4 exception and peripheral interrupt handlers */
#include "board.h"
#include "console.h"
#include "pulse.h"
#include "quad.h"

void NMI_Handler(void)
{
  for (;;) {
  }
}

/* Orange LED solid + green off = fault. Attach a debugger and inspect
 * SCB->CFSR / SCB->HFSR / SCB->BFAR / SCB->MMFAR and the stacked PC.
 * The pulse timer is stopped first: it runs in hardware and would otherwise
 * keep sending pulses from a crashed board. */
void HardFault_Handler(void)
{
  TIM2->CR1 &= ~TIM_CR1_CEN;
  TIM1->CR1 &= ~TIM_CR1_CEN;
  HAL_GPIO_WritePin(LED_GREEN_PORT, LED_GREEN_PIN, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LED_ORANGE_PORT, LED_ORANGE_PIN, GPIO_PIN_SET);
  for (;;) {
  }
}

void MemManage_Handler(void)
{
  HardFault_Handler();
}

void BusFault_Handler(void)
{
  HardFault_Handler();
}

void UsageFault_Handler(void)
{
  HardFault_Handler();
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}

void TIM2_IRQHandler(void)
{
  pulse_irq_handler();
}

void TIM1_CC_IRQHandler(void)
{
  quad_irq_handler();
}

void USART1_IRQHandler(void)
{
  con_irq_handler();
}
