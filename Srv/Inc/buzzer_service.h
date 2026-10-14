/**
  ******************************************************************************
  * @file           : buzzer_service.h
  * @brief          : Non-blocking audible alert service.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 01.08.2026
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

#ifndef BUZZER_SERVICE_H
#define BUZZER_SERVICE_H

#include "main.h"

/** @brief Initialize PWM, create the service task, and schedule a self-test. */
ErrorStatus BuzzerService_Init(void);

/**
  * @brief Schedule SOS followed by resource identification beeps.
  * @param resourceIndex (uint8_t) Zero-based resource slot; it is identified
  *        audibly by resourceIndex + 1 beeps.
  * @retval (ErrorStatus) SUCCESS when the service accepted the request.
  */
ErrorStatus BuzzerService_Alert(uint8_t resourceIndex);

/**
  * @brief Schedule one SOS for all resources selected by a bit mask.
  * @param resourceMask (uint8_t) Bit N selects zero-based resource slot N.
  */
ErrorStatus BuzzerService_AlertResources(uint8_t resourceMask);

/**
  * @brief Schedule an expiry melody followed by resource identification beeps.
  * @param resourceIndex (uint8_t) Zero-based resource slot; it is identified
  *        audibly by resourceIndex + 1 beeps.
  */
ErrorStatus BuzzerService_CertificateExpiryWarning(uint8_t resourceIndex);

/**
  * @brief Schedule one expiry melody for all resources selected by a bit mask.
  * @param resourceMask (uint8_t) Bit N selects zero-based resource slot N.
  */
ErrorStatus BuzzerService_CertificateExpiryWarnings(uint8_t resourceMask);

/**
  * @brief Atomically schedule the alert classes detected in one check cycle.
  * @param failedMask (uint8_t) Failed-resource bit mask.
  * @param certificateMask (uint8_t) Expiring-certificate bit mask.
  */
ErrorStatus BuzzerService_QueueAlerts(
  uint8_t failedMask,
  uint8_t certificateMask
);

/**
  * @brief Play the five-beep factory-reset warning and wait for completion.
  * @retval (ErrorStatus) SUCCESS after all five tones have completed.
  */
ErrorStatus BuzzerService_FactoryResetWarning(void);

/** @brief Play the three-beep factory-reset cancellation acknowledgement. */
ErrorStatus BuzzerService_FactoryResetCancelled(void);

#endif /* BUZZER_SERVICE_H */
