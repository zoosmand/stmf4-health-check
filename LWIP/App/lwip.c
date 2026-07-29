/**
  ******************************************************************************
  * @file           : lwip.c
  * @brief          : FreeRTOS-aware lwIP initialization and Ethernet polling.
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
#include "lwip/dns.h"
#include "lwip/tcpip.h"
#include "netif/ethernet.h"

#include <stdio.h>

#define LWIP_LINK_POLL_PERIOD_MS 100U
#define LWIP_DHCP_TIMEOUT_MS     10000U
#define LWIP_INIT_TIMEOUT_MS     5000U

struct netif gnetif;

static sys_sem_t lwipInitSemaphore;
static volatile Lwip_StatusTypeDef lwipInitStatus;
static uint32_t lwipLinkPollTick;
static uint32_t lwipDhcpWaitStartTick;
static uint8_t lwipNetworkConfigurationReported;
static uint8_t lwipStaticFallbackActive;
static volatile uint8_t lwipNetworkReady;

static void lwip_CoreInit(void* argument);
static void lwip_LinkStatusChanged(struct netif* netif);
static void lwip_StartDhcp(void);
static void lwip_ApplyStaticConfiguration(void);
static void lwip_ReportNetworkConfiguration(const char* source);
static void lwip_ProcessConfiguration(uint32_t now);

Lwip_StatusTypeDef Lwip_Init(void) {
  if (sys_sem_new(&lwipInitSemaphore, 0U) != ERR_OK)
    return LWIP_STATUS_INTERFACE_ERROR;

  lwipInitStatus = LWIP_STATUS_INTERFACE_ERROR;
  tcpip_init(lwip_CoreInit, NULL);
  if (sys_arch_sem_wait(
        &lwipInitSemaphore,
        LWIP_INIT_TIMEOUT_MS
      ) == SYS_ARCH_TIMEOUT) {
    sys_sem_free(&lwipInitSemaphore);
    return LWIP_STATUS_INTERFACE_ERROR;
  }

  sys_sem_free(&lwipInitSemaphore);
  return lwipInitStatus;
}

void Lwip_Process(void) {
  ethernetif_input(&gnetif);

  uint32_t now = HAL_GetTick();
  if ((now - lwipLinkPollTick) < LWIP_LINK_POLL_PERIOD_MS)
    return;

  lwipLinkPollTick = now;
  LOCK_TCPIP_CORE();
  ethernet_link_check_state(&gnetif);
  lwip_ProcessConfiguration(now);
  UNLOCK_TCPIP_CORE();
}

uint8_t Lwip_IsReady(void) {
  return lwipNetworkReady;
}

/**
  * @brief Initialize the netif from the exclusive lwIP TCP/IP thread.
  * @param argument (void*) Unused initialization argument.
  */
static void lwip_CoreInit(void* argument) {
  (void)argument;
  ip4_addr_t address = {0};
  ip4_addr_t netmask = {0};
  ip4_addr_t gateway = {0};

  if (netif_add(
        &gnetif,
        &address,
        &netmask,
        &gateway,
        NULL,
        ethernetif_init,
        tcpip_input
      ) == NULL) {
    lwipInitStatus = LWIP_STATUS_INTERFACE_ERROR;
    sys_sem_signal(&lwipInitSemaphore);
    return;
  }

  netif_set_default(&gnetif);
  netif_set_link_callback(&gnetif, lwip_LinkStatusChanged);
  lwipDhcpWaitStartTick = HAL_GetTick();
  lwipNetworkConfigurationReported = 0U;
  lwipStaticFallbackActive = 0U;
  lwipNetworkReady = 0U;

  if (netif_is_link_up(&gnetif)) {
    netif_set_up(&gnetif);
    lwip_StartDhcp();
  } else {
    netif_set_down(&gnetif);
  }

  lwipInitStatus = LWIP_STATUS_OK;
  sys_sem_signal(&lwipInitSemaphore);
}

/**
  * @brief Process DHCP completion and static fallback from the core lock.
  * @param now (uint32_t) Current HAL tick in milliseconds.
  */
static void lwip_ProcessConfiguration(uint32_t now) {
  if (!netif_is_link_up(&gnetif)) {
    if (ip4_addr_isany_val(*netif_ip4_addr(&gnetif))
        && ((now - lwipDhcpWaitStartTick) >= LWIP_DHCP_TIMEOUT_MS)) {
      lwip_ApplyStaticConfiguration();
    }
    return;
  }

  if (dhcp_supplied_address(&gnetif) != 0U) {
    if ((lwipNetworkConfigurationReported == 0U)
        || (lwipStaticFallbackActive != 0U)) {
      lwipStaticFallbackActive = 0U;
      lwip_ReportNetworkConfiguration("DHCP");
    }
    return;
  }

  if (!ip4_addr_isany_val(*netif_ip4_addr(&gnetif)))
    return;

  if ((now - lwipDhcpWaitStartTick) >= LWIP_DHCP_TIMEOUT_MS)
    lwip_ApplyStaticConfiguration();
}

static void lwip_LinkStatusChanged(struct netif* netif) {
  if (netif_is_link_up(netif)) {
    lwipNetworkConfigurationReported = 0U;
    lwip_StartDhcp();
    return;
  }

  dhcp_stop(netif);
  lwipDhcpWaitStartTick = HAL_GetTick();
  lwipNetworkConfigurationReported = 0U;
  lwipStaticFallbackActive = 0U;
  lwipNetworkReady = 0U;
}

/**
  * @brief Start DHCP after the Ethernet interface is up.
  */
static void lwip_StartDhcp(void) {
  netif_set_addr(
    &gnetif,
    IP4_ADDR_ANY4,
    IP4_ADDR_ANY4,
    IP4_ADDR_ANY4
  );
  dns_setserver(0U, IP_ADDR_ANY);
  lwipDhcpWaitStartTick = HAL_GetTick();
  lwipStaticFallbackActive = 0U;
  lwipNetworkReady = 0U;
  if (dhcp_start(&gnetif) != ERR_OK)
    lwip_ApplyStaticConfiguration();
}

/**
  * @brief Configure the documented fallback IPv4 parameters.
  */
static void lwip_ApplyStaticConfiguration(void) {
  ip4_addr_t address;
  ip4_addr_t netmask;
  ip4_addr_t gateway;
  ip_addr_t dnsServer;

  IP4_ADDR(&address, 192U, 168U, 0U, 50U);
  IP4_ADDR(&netmask, 255U, 255U, 255U, 0U);
  IP4_ADDR(&gateway, 192U, 168U, 0U, 1U);
  IP_ADDR4(&dnsServer, 8U, 8U, 8U, 8U);

  if (!netif_is_link_up(&gnetif))
    dhcp_stop(&gnetif);

  netif_set_addr(&gnetif, &address, &netmask, &gateway);
  dns_setserver(0U, &dnsServer);
  lwipStaticFallbackActive = 1U;
  lwip_ReportNetworkConfiguration("static fallback");
}

/**
  * @brief Print the active IPv4 address, mask, gateway, and primary DNS.
  * @param source (const char*) Human-readable configuration source.
  */
static void lwip_ReportNetworkConfiguration(const char* source) {
  const ip4_addr_t* address = netif_ip4_addr(&gnetif);
  const ip4_addr_t* netmask = netif_ip4_netmask(&gnetif);
  const ip4_addr_t* gateway = netif_ip4_gw(&gnetif);
  const ip_addr_t* dnsServer = dns_getserver(0U);
  const ip4_addr_t* dnsAddress = ip_2_ip4(dnsServer);

  printf(
    "ETH configuration: %s\r\n"
    "ETH IP: %u.%u.%u.%u\r\n"
    "ETH MASK: %u.%u.%u.%u\r\n"
    "ETH GW: %u.%u.%u.%u\r\n"
    "ETH DNS: %u.%u.%u.%u\r\n",
    source,
    (unsigned int)ip4_addr1(address),
    (unsigned int)ip4_addr2(address),
    (unsigned int)ip4_addr3(address),
    (unsigned int)ip4_addr4(address),
    (unsigned int)ip4_addr1(netmask),
    (unsigned int)ip4_addr2(netmask),
    (unsigned int)ip4_addr3(netmask),
    (unsigned int)ip4_addr4(netmask),
    (unsigned int)ip4_addr1(gateway),
    (unsigned int)ip4_addr2(gateway),
    (unsigned int)ip4_addr3(gateway),
    (unsigned int)ip4_addr4(gateway),
    (unsigned int)ip4_addr1(dnsAddress),
    (unsigned int)ip4_addr2(dnsAddress),
    (unsigned int)ip4_addr3(dnsAddress),
    (unsigned int)ip4_addr4(dnsAddress)
  );
  lwipNetworkConfigurationReported = 1U;
  lwipNetworkReady = netif_is_link_up(&gnetif) ? 1U : 0U;
}
