/**
  ******************************************************************************
  * @file           : ethernetif.h
  * @brief          : STM32 Ethernet MAC and DP83848 lwIP interface.
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

#ifndef ETHERNETIF_H
#define ETHERNETIF_H

#include "lwip/err.h"
#include "lwip/netif.h"

/** @brief Initialize the STM32 MAC, DMA rings, PHY, and lwIP netif fields. */
err_t ethernetif_init(struct netif* netif);

/** @brief Drain received Ethernet frames into lwIP from network-task context. */
void ethernetif_input(struct netif* netif);

/** @brief Poll DP83848 link state and apply negotiated MAC speed and duplex. */
void ethernet_link_check_state(struct netif* netif);

/** @brief Provide lwIP with the wrapping platform time in milliseconds. */
u32_t sys_now(void);

#endif /* ETHERNETIF_H */
