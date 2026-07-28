/**
  ******************************************************************************
  * @file           : lwip.c
  * @brief          : Bare-metal lwIP initialization and polling.
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

#include "lwip.h"

#include "ethernetif.h"
#include "lwip/dhcp.h"
#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"

#define LWIP_LINK_POLL_PERIOD_MS 100U

struct netif gnetif;

static uint32_t lwipLinkPollTick;
static void lwip_LinkStatusChanged(struct netif* netif);

Lwip_StatusTypeDef Lwip_Init(void) {
  ip4_addr_t address = {0};
  ip4_addr_t netmask = {0};
  ip4_addr_t gateway = {0};

  lwip_init();

  if (netif_add(
        &gnetif,
        &address,
        &netmask,
        &gateway,
        NULL,
        ethernetif_init,
        ethernet_input
      ) == NULL) {
    return LWIP_STATUS_INTERFACE_ERROR;
  }

  netif_set_default(&gnetif);
  netif_set_link_callback(&gnetif, lwip_LinkStatusChanged);
  if (netif_is_link_up(&gnetif))
    netif_set_up(&gnetif);
  else
    netif_set_down(&gnetif);

  if (dhcp_start(&gnetif) != ERR_OK)
    return LWIP_STATUS_DHCP_ERROR;

  return LWIP_STATUS_OK;
}

void Lwip_Process(void) {
  ethernetif_input(&gnetif);
  sys_check_timeouts();

  uint32_t now = HAL_GetTick();
  if ((now - lwipLinkPollTick) >= LWIP_LINK_POLL_PERIOD_MS) {
    lwipLinkPollTick = now;
    ethernet_link_check_state(&gnetif);
  }
}

static void lwip_LinkStatusChanged(struct netif* netif) {
  (void)netif;
}
