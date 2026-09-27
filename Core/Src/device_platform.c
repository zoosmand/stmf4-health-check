#include "platform.h"

static volatile uint32_t platformTick;

void Platform_Init(void) {
  SCB->CPACR |= (3UL << (10U * 2U)) | (3UL << (11U * 2U));
  FLASH->ACR |= FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_PRFTEN;
  NVIC_SetPriorityGrouping(3U);
  SystemCoreClockUpdate();
  if (SysTick_Config(SystemCoreClock / 1000U) != 0U) {
    for (;;) {
    }
  }
  NVIC_SetPriority(SysTick_IRQn, (1UL << __NVIC_PRIO_BITS) - 1UL);
}

void Platform_IncrementTick(void) { ++platformTick; }
uint32_t Platform_GetTick(void) { return platformTick; }

void Platform_Delay(uint32_t milliseconds) {
  uint32_t started = Platform_GetTick();
  while ((Platform_GetTick() - started) < milliseconds)
    __NOP();
}

void Platform_GpioConfigure(
  GPIO_TypeDef* port,
  uint8_t pin,
  uint32_t mode,
  uint32_t pull,
  uint32_t speed,
  uint8_t alternate
) {
  uint32_t shift = (uint32_t)pin * 2U;
  port->MODER = (port->MODER & ~(3UL << shift)) | (mode << shift);
  port->PUPDR = (port->PUPDR & ~(3UL << shift)) | (pull << shift);
  port->OSPEEDR = (port->OSPEEDR & ~(3UL << shift)) | (speed << shift);
  if (mode == 2U) {
    uint32_t index = pin / 8U;
    uint32_t alternateShift = (uint32_t)(pin % 8U) * 4U;
    port->AFR[index] = (port->AFR[index] & ~(15UL << alternateShift))
      | ((uint32_t)alternate << alternateShift);
  }
}
