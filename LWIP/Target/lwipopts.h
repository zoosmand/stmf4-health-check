/**
  ******************************************************************************
  * @file           : lwipopts.h
  * @brief          : lwIP configuration for bare-metal Ethernet operation.
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

#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#include "main.h"

#define WITH_RTOS                    0
#define NO_SYS                       1
#define SYS_LIGHTWEIGHT_PROT         0

#define LWIP_IPV4                    1
#define LWIP_IPV6                    0
#define LWIP_ETHERNET                1
#define LWIP_ARP                     1
#define LWIP_DHCP                    1
#define LWIP_AUTOIP                  0
#define LWIP_DNS                     1
#define LWIP_IGMP                    0
#define LWIP_NETIF_LINK_CALLBACK     1

#define LWIP_NETCONN                 0
#define LWIP_SOCKET                  0
#define LWIP_STATS                   0

#define MEM_ALIGNMENT                4
#define MEM_SIZE                     (10U * 1024U)

#define CHECKSUM_BY_HARDWARE         1
#define CHECKSUM_GEN_IP              0
#define CHECKSUM_GEN_UDP             0
#define CHECKSUM_GEN_TCP             0
#define CHECKSUM_GEN_ICMP            0
#define CHECKSUM_GEN_ICMP6           0
#define CHECKSUM_CHECK_IP            0
#define CHECKSUM_CHECK_UDP           0
#define CHECKSUM_CHECK_TCP           0
#define CHECKSUM_CHECK_ICMP          0
#define CHECKSUM_CHECK_ICMP6         0

#define LWIP_DNS_SECURE              7
#define TCP_SND_QUEUELEN             9
#define TCP_SNDLOWAT                 1071
#define TCP_SNDQUEUELOWAT            5
#define TCP_WND_UPDATE_THRESHOLD     536

#endif /* LWIPOPTS_H */
