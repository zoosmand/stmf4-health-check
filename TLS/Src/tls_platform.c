/**
  ******************************************************************************
  * @file           : tls_platform.c
  * @brief          : STM32 entropy, UTC, and bounded allocator for Mbed TLS.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 30.07.2026
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

#include "tls_platform.h"

#include "mbedtls/entropy.h"
#include "mbedtls/memory_buffer_alloc.h"
#include "mbedtls/platform.h"
#include "mbedtls/platform_time.h"
#include "psa/crypto.h"
#include "rtc.h"

#include <stdlib.h>
#include <string.h>

#define TLS_PLATFORM_HEAP_SIZE (52U * 1024U)

static RNG_HandleTypeDef randomGenerator;
static uint8_t tlsHeap[TLS_PLATFORM_HEAP_SIZE]
  __attribute__((section(".ccmram"), aligned(8)));

static mbedtls_time_t tlsPlatform_GetTime(mbedtls_time_t* currentTime) {
  uint32_t unixTime = 0U;
  if ((Rtc_IsSynchronized() == 0U)
      || (Rtc_GetUnixTime(&unixTime) != HAL_OK)) {
    return 0;
  }

  if (currentTime != NULL)
    *currentTime = (mbedtls_time_t)unixTime;
  return (mbedtls_time_t)unixTime;
}

HAL_StatusTypeDef TlsPlatform_Init(void) {
  randomGenerator.Instance = RNG;
  if (HAL_RNG_Init(&randomGenerator) != HAL_OK)
    return HAL_ERROR;

  uint32_t randomSeed;
  if (HAL_RNG_GenerateRandomNumber(
        &randomGenerator,
        &randomSeed
      ) != HAL_OK) {
    return HAL_ERROR;
  }
  srand(randomSeed);

  mbedtls_memory_buffer_alloc_init(tlsHeap, sizeof(tlsHeap));
  if (mbedtls_platform_set_time(tlsPlatform_GetTime) != 0)
    return HAL_ERROR;
  if (psa_crypto_init() != PSA_SUCCESS)
    return HAL_ERROR;
  return HAL_OK;
}

int mbedtls_hardware_poll(
  void* data,
  unsigned char* output,
  size_t length,
  size_t* outputLength
) {
  (void)data;
  if ((output == NULL) || (outputLength == NULL))
    return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;

  size_t offset = 0U;
  while (offset < length) {
    uint32_t randomValue;
    if (HAL_RNG_GenerateRandomNumber(
          &randomGenerator,
          &randomValue
        ) != HAL_OK) {
      *outputLength = offset;
      return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }

    size_t chunk = length - offset;
    if (chunk > sizeof(randomValue))
      chunk = sizeof(randomValue);
    memcpy(&output[offset], &randomValue, chunk);
    offset += chunk;
  }

  *outputLength = offset;
  return 0;
}

mbedtls_ms_time_t mbedtls_ms_time(void) {
  return (mbedtls_ms_time_t)HAL_GetTick();
}
