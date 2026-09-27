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

#include "health_check_log.h"
#include "main.h"

/**
  * @brief Create the static callback queue and delivery task.
  * @retval (ErrorStatus) SUCCESS when both objects were created.
  */
ErrorStatus CallbackService_Init(void);

/**
  * @brief Queue one persisted failed check without blocking its producer.
  * @param entry (const HealthCheckLog_EntryTypeDef*) Non-null verified log
  *        entry for the failed check.
  * @note If the three-entry queue is full, the oldest pending event is dropped.
  */
void CallbackService_Enqueue(const HealthCheckLog_EntryTypeDef* entry);

#endif /* CALLBACK_SERVICE_H */
