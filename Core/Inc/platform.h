#ifndef PLATFORM_H
#define PLATFORM_H

#include "stm32f4xx.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
  PLATFORM_STATUS_OK = 0,
  PLATFORM_STATUS_ERROR,
  PLATFORM_STATUS_TIMEOUT,
} Platform_StatusTypeDef;

#define PLATFORM_GPIO_MODE_INPUT      0U
#define PLATFORM_GPIO_MODE_OUTPUT     1U
#define PLATFORM_GPIO_MODE_ALTERNATE  2U
#define PLATFORM_GPIO_PULL_NONE       0U
#define PLATFORM_GPIO_PULL_UP         1U
#define PLATFORM_GPIO_PULL_DOWN       2U
#define PLATFORM_GPIO_SPEED_VERY_HIGH 3U

void Platform_Init(void);
void Platform_IncrementTick(void);
uint32_t Platform_GetTick(void);
void Platform_Delay(uint32_t milliseconds);

void Platform_GpioConfigure(
  GPIO_TypeDef* port,
  uint8_t pin,
  uint32_t mode,
  uint32_t pull,
  uint32_t speed,
  uint8_t alternate
);

static inline void Platform_GpioWrite(
  GPIO_TypeDef* port,
  uint8_t pin,
  uint8_t high
) {
  port->BSRR = high != 0U ? (1UL << pin) : (1UL << (pin + 16U));
}

static inline uint8_t Platform_GpioRead(GPIO_TypeDef* port, uint8_t pin) {
  return (port->IDR & (1UL << pin)) != 0U;
}

#endif /* PLATFORM_H */
