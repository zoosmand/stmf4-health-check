/**
  ******************************************************************************
  * @file           : lwip.h
  * @brief          : Bare-metal lwIP integration interface.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 13.01.2026
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2017-2026 Dmitry Slobodchikov
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

#ifndef LWIP_APP_H
#define LWIP_APP_H

#include "lwip/netif.h"

typedef enum {
  LWIP_STATUS_OK = 0,
  LWIP_STATUS_INTERFACE_ERROR,
  LWIP_STATUS_DHCP_ERROR,
} Lwip_StatusTypeDef;

extern struct netif gnetif;

/**
  * @brief Initialize lwIP, the Ethernet interface, and DHCP.
  * @retval (Lwip_StatusTypeDef) Initialization result.
  */
Lwip_StatusTypeDef Lwip_Init(void);

/**
  * @brief Poll received frames, protocol timers, and PHY link state.
  */
void Lwip_Process(void);

/**
  * @brief Check whether a configured Ethernet link is available.
  * @retval (uint8_t) Nonzero when link and IPv4 configuration are ready.
  */
uint8_t Lwip_IsReady(void);

#endif /* LWIP_APP_H */
