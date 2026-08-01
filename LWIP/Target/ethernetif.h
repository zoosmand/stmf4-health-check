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

err_t ethernetif_init(struct netif* netif);
void ethernetif_input(struct netif* netif);
void ethernet_link_check_state(struct netif* netif);
u32_t sys_now(void);

#endif /* ETHERNETIF_H */
