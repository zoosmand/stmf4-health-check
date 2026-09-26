#include "callback_service.h"

#include "FreeRTOS.h"
#include "callback_config.h"
#include "queue.h"
#include "task.h"

#include <stdio.h>
#include <string.h>

#define CALLBACK_TASK_STACK_DEPTH 2048U
#define CALLBACK_QUEUE_LENGTH     3U
#define CALLBACK_BODY_SIZE        192U
#define CALLBACK_RESOURCE_SIZE    256U

typedef struct {
  uint8_t resource;
  uint8_t healthy;
  uint16_t httpStatus;
  uint32_t elapsedMs;
} CallbackService_EventTypeDef;

static StaticTask_t callbackTaskControlBlock;
static StackType_t callbackTaskStack[CALLBACK_TASK_STACK_DEPTH];
static StaticQueue_t callbackQueueControlBlock;
static uint8_t callbackQueueStorage[
  CALLBACK_QUEUE_LENGTH * sizeof(CallbackService_EventTypeDef)
];
static QueueHandle_t callbackQueue;

/**
  * @brief Consume queued check results and deliver enabled callbacks.
  * @param argument (void*) Unused FreeRTOS task argument.
  * @note Delivery may block on bounded DNS/TCP/TLS timeouts, but never blocks
  *       the health-check producer because events cross a finite queue.
  */
static void callbackService_Task(void* argument) {
  (void)argument;
  CallbackService_EventTypeDef event;
  for (;;) {
    if (xQueueReceive(callbackQueue, &event, portMAX_DELAY) != pdTRUE)
      continue;
    CallbackConfig_TypeDef config;
    CallbackConfig_Get(&config);
    if (config.enabled == 0U)
      continue;
    const char* status = event.healthy != 0U ? "ok" : "failed";
    char body[CALLBACK_BODY_SIZE];
    char resource[CALLBACK_RESOURCE_SIZE];
    const char* method;
    const char* contentType = NULL;
    if (config.method == CALLBACK_METHOD_POST) {
      method = "POST";
      int length = snprintf(
        body, sizeof(body),
        "{\"resource\":%u,\"status\":\"%s\",\"http_status\":%u,"
        "\"elapsed_ms\":%lu}",
        (unsigned int)event.resource, status,
        (unsigned int)event.httpStatus, (unsigned long)event.elapsedMs
      );
      if ((length <= 0) || ((size_t)length >= sizeof(body)))
        continue;
      (void)strncpy(resource, config.path, sizeof(resource) - 1U);
      resource[sizeof(resource) - 1U] = '\0';
      contentType = "application/json";
    } else {
      method = "GET";
      const char separator = strchr(config.path, '?') == NULL ? '?' : '&';
      int length = snprintf(
        resource, sizeof(resource),
        "%s%cresource=%u&status=%s&http_status=%u&elapsed_ms=%lu",
        config.path, separator, (unsigned int)event.resource, status,
        (unsigned int)event.httpStatus, (unsigned long)event.elapsedMs
      );
      if ((length <= 0) || ((size_t)length >= sizeof(resource)))
        continue;
      body[0] = '\0';
    }
    TlsTransport_ResultTypeDef callbackResult;
    (void)TlsTransport_Request(
      method, config.host, config.port, resource,
      config.trustAnchorId, body, contentType, &callbackResult
    );
    printf(
      "Callback: resource=%u transport=%u http=%u detail=%d\r\n",
      (unsigned int)event.resource, (unsigned int)callbackResult.status,
      (unsigned int)callbackResult.httpStatus, callbackResult.detail
    );
  }
}

ErrorStatus CallbackService_Init(void) {
  callbackQueue = xQueueCreateStatic(
    CALLBACK_QUEUE_LENGTH,
    sizeof(CallbackService_EventTypeDef),
    callbackQueueStorage,
    &callbackQueueControlBlock
  );
  if (callbackQueue == NULL)
    return ERROR;
  return xTaskCreateStatic(
    callbackService_Task, "callback", CALLBACK_TASK_STACK_DEPTH, NULL,
    tskIDLE_PRIORITY + 1U, callbackTaskStack, &callbackTaskControlBlock
  ) != NULL ? SUCCESS : ERROR;
}

void CallbackService_Enqueue(
  uint8_t resource,
  uint8_t healthy,
  const TlsTransport_ResultTypeDef* result
) {
  if ((callbackQueue == NULL) || (result == NULL))
    return;
  CallbackService_EventTypeDef event = {
    .resource = resource,
    .healthy = healthy,
    .httpStatus = result->httpStatus,
    .elapsedMs = result->elapsedMs,
  };
  if (xQueueSendToBack(callbackQueue, &event, 0U) != pdTRUE) {
    CallbackService_EventTypeDef discarded;
    (void)xQueueReceive(callbackQueue, &discarded, 0U);
    (void)xQueueSendToBack(callbackQueue, &event, 0U);
    printf("Callback: queue full; dropping oldest result.\r\n");
  }
}
