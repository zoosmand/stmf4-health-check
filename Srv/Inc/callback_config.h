#ifndef CALLBACK_CONFIG_H
#define CALLBACK_CONFIG_H

#include "main.h"

#include <stdint.h>

#define CALLBACK_CONFIG_HOST_SIZE 64U
#define CALLBACK_CONFIG_PATH_SIZE 80U

typedef enum {
  CALLBACK_METHOD_GET = 0,
  CALLBACK_METHOD_POST = 1
} CallbackConfig_MethodTypeDef;

typedef struct {
  uint8_t enabled;
  uint8_t method;
  uint8_t trustAnchorId;
  uint8_t reserved;
  uint16_t port;
  char host[CALLBACK_CONFIG_HOST_SIZE];
  char path[CALLBACK_CONFIG_PATH_SIZE];
} CallbackConfig_TypeDef;

Platform_StatusTypeDef CallbackConfig_Init(void);
void CallbackConfig_Get(CallbackConfig_TypeDef* config);
Platform_StatusTypeDef CallbackConfig_Set(const CallbackConfig_TypeDef* config);
uint8_t CallbackConfig_IsTrustAnchorInUse(uint8_t trustAnchorId);

#endif /* CALLBACK_CONFIG_H */
