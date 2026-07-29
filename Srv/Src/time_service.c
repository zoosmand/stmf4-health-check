/**
  ******************************************************************************
  * @file           : time_service.c
  * @brief          : NTP synchronization and RTC reporting service.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 29.07.2026
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

#include "time_service.h"

#include "FreeRTOS.h"
#include "lwip.h"
#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/tcpip.h"
#include "lwip/udp.h"
#include "rtc.h"
#include "task.h"

#include <stdio.h>
#include <string.h>

#define TIME_TASK_STACK_DEPTH       256U
#define TIME_TASK_PERIOD_MS         1000U
#define TIME_NTP_RETRY_SECONDS      60U
#define TIME_NTP_RESYNC_SECONDS     3600U
#define TIME_NTP_TIMEOUT_SECONDS    10U
#define TIME_REPORT_SECONDS         60U
#define TIME_NTP_PORT               123U
#define TIME_NTP_PACKET_SIZE        48U
#define TIME_NTP_UNIX_EPOCH_OFFSET  2208988800UL
#define TIME_NTP_SERVER             "pool.ntp.org"

typedef enum {
  TIME_SYNC_IDLE = 0,
  TIME_SYNC_PENDING,
  TIME_SYNC_RECEIVED,
  TIME_SYNC_FAILED
} TimeService_SyncStateTypeDef;

static StaticTask_t timeTaskControlBlock;
static StackType_t timeTaskStack[TIME_TASK_STACK_DEPTH];
static struct udp_pcb* ntpPcb;
static volatile TimeService_SyncStateTypeDef syncState;
static volatile uint32_t receivedUnixTime;

static void timeService_Task(void* argument);
static void timeService_StartRequest(void* argument);
static void timeService_DnsCallback(
  const char* name,
  const ip_addr_t* address,
  void* argument
);
static void timeService_SendRequest(const ip_addr_t* address);
static void timeService_Receive(
  void* argument,
  struct udp_pcb* pcb,
  struct pbuf* packet,
  const ip_addr_t* address,
  u16_t port
);

ErrorStatus TimeService_Init(void) {
  syncState = TIME_SYNC_IDLE;
  receivedUnixTime = 0U;

  TaskHandle_t task = xTaskCreateStatic(
    timeService_Task,
    "time",
    TIME_TASK_STACK_DEPTH,
    NULL,
    tskIDLE_PRIORITY + 1U,
    timeTaskStack,
    &timeTaskControlBlock
  );
  return task != NULL ? SUCCESS : ERROR;
}

static void timeService_Task(void* argument) {
  (void)argument;

  uint32_t retrySeconds = 0U;
  uint32_t reportSeconds = TIME_REPORT_SECONDS;
  uint32_t requestSeconds = 0U;
  uint8_t waitingReported = 0U;

  if (Rtc_IsSynchronized())
    printf("RTC: retained synchronized UTC time.\r\n");
  else
    printf("RTC: awaiting NTP synchronization.\r\n");

  for (;;) {
    if (!Lwip_IsReady()) {
      if (!waitingReported) {
        printf("NTP: waiting for network.\r\n");
        waitingReported = 1U;
      }
    } else {
      waitingReported = 0U;

      if ((syncState == TIME_SYNC_RECEIVED) && (receivedUnixTime != 0U)) {
        uint32_t unixTime = receivedUnixTime;
        receivedUnixTime = 0U;
        if (Rtc_SetUnixTime(unixTime) == HAL_OK) {
          printf("NTP: synchronized with " TIME_NTP_SERVER ".\r\n");
          syncState = TIME_SYNC_IDLE;
          retrySeconds = TIME_NTP_RESYNC_SECONDS;
          requestSeconds = 0U;
          reportSeconds = TIME_REPORT_SECONDS;
        } else {
          syncState = TIME_SYNC_FAILED;
        }
      }

      if (syncState == TIME_SYNC_FAILED) {
        printf("NTP: synchronization failed; retry in %u seconds.\r\n",
          TIME_NTP_RETRY_SECONDS);
        syncState = TIME_SYNC_IDLE;
        retrySeconds = TIME_NTP_RETRY_SECONDS;
        requestSeconds = 0U;
      }

      if ((retrySeconds == 0U) && (syncState == TIME_SYNC_IDLE)) {
        printf("NTP: synchronizing with " TIME_NTP_SERVER ".\r\n");
        syncState = TIME_SYNC_PENDING;
        requestSeconds = 0U;
        if (tcpip_callback(timeService_StartRequest, NULL) != ERR_OK)
          syncState = TIME_SYNC_FAILED;
      } else if (retrySeconds > 0U) {
        --retrySeconds;
      }

      if ((syncState == TIME_SYNC_PENDING)
          && (++requestSeconds >= TIME_NTP_TIMEOUT_SECONDS)) {
        syncState = TIME_SYNC_FAILED;
      }
    }

    if (Rtc_IsSynchronized()) {
      if (reportSeconds >= TIME_REPORT_SECONDS) {
        Rtc_DateTimeTypeDef dateTime;
        if (Rtc_GetDateTime(&dateTime) == HAL_OK) {
          printf("RTC UTC: %04u-%02u-%02u %02u:%02u:%02u\r\n",
            dateTime.year, dateTime.month, dateTime.day,
            dateTime.hour, dateTime.minute, dateTime.second);
        }
        reportSeconds = 0U;
      }
      ++reportSeconds;
    }

    vTaskDelay(pdMS_TO_TICKS(TIME_TASK_PERIOD_MS));
  }
}

static void timeService_StartRequest(void* argument) {
  (void)argument;

  if (ntpPcb == NULL) {
    ntpPcb = udp_new();
    if (ntpPcb == NULL) {
      syncState = TIME_SYNC_FAILED;
      return;
    }
    udp_recv(ntpPcb, timeService_Receive, NULL);
  }

  ip_addr_t address;
  err_t result = dns_gethostbyname(
    TIME_NTP_SERVER,
    &address,
    timeService_DnsCallback,
    NULL
  );
  if (result == ERR_OK)
    timeService_SendRequest(&address);
  else if (result != ERR_INPROGRESS)
    syncState = TIME_SYNC_FAILED;
}

static void timeService_DnsCallback(
  const char* name,
  const ip_addr_t* address,
  void* argument
) {
  (void)name;
  (void)argument;

  if (address == NULL) {
    syncState = TIME_SYNC_FAILED;
    return;
  }
  timeService_SendRequest(address);
}

static void timeService_SendRequest(const ip_addr_t* address) {
  err_t result = udp_connect(ntpPcb, address, TIME_NTP_PORT);
  if (result != ERR_OK) {
    syncState = TIME_SYNC_FAILED;
    return;
  }

  struct pbuf* packet = pbuf_alloc(
    PBUF_TRANSPORT,
    TIME_NTP_PACKET_SIZE,
    PBUF_RAM
  );
  if (packet == NULL) {
    syncState = TIME_SYNC_FAILED;
    return;
  }

  memset(packet->payload, 0, TIME_NTP_PACKET_SIZE);
  ((uint8_t*)packet->payload)[0] = 0x23U;
  result = udp_send(ntpPcb, packet);
  pbuf_free(packet);
  if (result != ERR_OK)
    syncState = TIME_SYNC_FAILED;
}

static void timeService_Receive(
  void* argument,
  struct udp_pcb* pcb,
  struct pbuf* packet,
  const ip_addr_t* address,
  u16_t port
) {
  (void)argument;
  (void)pcb;
  (void)address;

  uint8_t header[4];
  uint32_t networkSeconds;
  if ((packet == NULL)
      || (port != TIME_NTP_PORT)
      || (packet->tot_len < TIME_NTP_PACKET_SIZE)
      || (pbuf_copy_partial(packet, header, sizeof(header), 0U)
          != sizeof(header))
      || ((header[0] >> 6U) == 3U)
      || ((header[0] & 0x07U) != 4U)
      || (header[1] == 0U)
      || (header[1] > 15U)
      || (pbuf_copy_partial(
          packet,
          &networkSeconds,
          sizeof(networkSeconds),
          40U
        ) != sizeof(networkSeconds))) {
    if (packet != NULL)
      pbuf_free(packet);
    syncState = TIME_SYNC_FAILED;
    return;
  }

  pbuf_free(packet);
  uint32_t ntpSeconds = lwip_ntohl(networkSeconds);
  if (ntpSeconds <= TIME_NTP_UNIX_EPOCH_OFFSET) {
    syncState = TIME_SYNC_FAILED;
    return;
  }

  receivedUnixTime = ntpSeconds - TIME_NTP_UNIX_EPOCH_OFFSET;
  syncState = TIME_SYNC_RECEIVED;
}
