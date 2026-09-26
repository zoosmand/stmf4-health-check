/**
  ******************************************************************************
  * @file           : callback_service.h
  * @brief          : Asynchronous outbound health-result callback service.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 26.09.2026
  ******************************************************************************
  */

#ifndef CALLBACK_SERVICE_H
#define CALLBACK_SERVICE_H

#include "main.h"
#include "tls_transport.h"

#include <stdint.h>

/**
  * @brief Create the static callback queue and delivery task.
  * @retval (ErrorStatus) SUCCESS when both objects were created.
  */
ErrorStatus CallbackService_Init(void);

/**
  * @brief Queue one completed check without blocking its producer.
  * @param resource (uint8_t) Zero-based health-check resource slot.
  * @param healthy (uint8_t) Nonzero for a successful HTTP 200 check.
  * @param result (const TlsTransport_ResultTypeDef*) Non-null transport result.
  * @note If the three-entry queue is full, the oldest pending event is dropped.
  */
void CallbackService_Enqueue(
  uint8_t resource,
  uint8_t healthy,
  const TlsTransport_ResultTypeDef* result
);

#endif /* CALLBACK_SERVICE_H */
