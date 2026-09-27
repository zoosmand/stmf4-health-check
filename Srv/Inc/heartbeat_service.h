/**
  ******************************************************************************
  * @file           : heartbeat_service.h
  * @brief          : Human-like status LED heartbeat service.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 25.09.2026
  ******************************************************************************
  */

#ifndef HEARTBEAT_SERVICE_H
#define HEARTBEAT_SERVICE_H

#include "main.h"

/** @brief Configure the status LED and create its heartbeat task. */
ErrorStatus HeartbeatService_Init(void);

#endif /* HEARTBEAT_SERVICE_H */
