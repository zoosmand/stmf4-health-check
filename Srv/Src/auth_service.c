/**
  ******************************************************************************
  * @file           : auth_service.c
  * @brief          : Persistent users and single-session bearer authorization.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 31.07.2026
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

#include "auth_service.h"

#include "FreeRTOS.h"
#include "master_password_credentials.h"
#include "mbedtls/constant_time.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/sha256.h"
#include "semphr.h"
#include "tls_platform.h"

#include <string.h>

#define AUTH_SERVICE_MASTER_USERNAME       "master"
#define AUTH_SERVICE_MAX_PASSWORD_LENGTH   128U
#define AUTH_SERVICE_MIN_PASSWORD_LENGTH   12U
#define AUTH_SERVICE_ACCESS_LIFETIME_SEC   900U
#define AUTH_SERVICE_REFRESH_LIFETIME_SEC  604800U
#define AUTH_SERVICE_TOKEN_BYTES           32U
#define AUTH_SERVICE_SESSION_COUNT         (USER_STORE_MAX_USERS + 1U)
#define AUTH_SERVICE_USER_ITERATIONS       100000U

typedef struct {
  uint8_t active;
  uint8_t role;
  char username[USER_STORE_USERNAME_SIZE];
  uint8_t accessDigest[32];
  uint8_t refreshDigest[32];
  uint32_t accessIssuedAt;
  uint32_t refreshIssuedAt;
} AuthService_SessionTypeDef;

static AuthService_SessionTypeDef sessions[AUTH_SERVICE_SESSION_COUNT];
static StaticSemaphore_t sessionMutexControlBlock;
static SemaphoreHandle_t sessionMutex;

static uint8_t authService_IsAdministrator(
  const AuthService_PrincipalTypeDef* principal
) {
  return ((principal != NULL)
      && (principal->role == USER_ROLE_ADMINISTRATOR))
    ? 1U
    : 0U;
}

static AuthService_StatusTypeDef authService_Derive(
  const uint8_t* password,
  size_t passwordLength,
  const uint8_t* salt,
  uint32_t iterations,
  uint8_t* verifier
) {
  if (TlsPlatform_Lock() != HAL_OK)
    return AUTH_SERVICE_STATUS_CRYPTO_ERROR;
  int result = mbedtls_pkcs5_pbkdf2_hmac_ext(
    MBEDTLS_MD_SHA256,
    password,
    passwordLength,
    salt,
    USER_STORE_SALT_SIZE,
    iterations,
    USER_STORE_VERIFIER_SIZE,
    verifier
  );
  TlsPlatform_Unlock();
  return (result == 0)
    ? AUTH_SERVICE_STATUS_OK
    : AUTH_SERVICE_STATUS_CRYPTO_ERROR;
}

static void authService_Hex(
  const uint8_t* input,
  size_t length,
  char* output
) {
  static const char digits[] = "0123456789abcdef";
  for (size_t index = 0U; index < length; ++index) {
    output[index * 2U] = digits[input[index] >> 4U];
    output[index * 2U + 1U] = digits[input[index] & 0x0FU];
  }
  output[length * 2U] = '\0';
}

static HAL_StatusTypeDef authService_HashToken(
  const char* token,
  uint8_t digest[32]
) {
  return (mbedtls_sha256(
    (const uint8_t*)token,
    strlen(token),
    digest,
    0
  ) == 0) ? HAL_OK : HAL_ERROR;
}

static AuthService_SessionTypeDef* authService_FindSession(
  const char* username
) {
  for (size_t index = 0U; index < AUTH_SERVICE_SESSION_COUNT; ++index) {
    if ((sessions[index].active != 0U)
        && (strcmp(sessions[index].username, username) == 0))
      return &sessions[index];
  }
  for (size_t index = 0U; index < AUTH_SERVICE_SESSION_COUNT; ++index) {
    if (sessions[index].active == 0U)
      return &sessions[index];
  }
  return NULL;
}

static AuthService_StatusTypeDef authService_Issue(
  const char* username,
  UserStore_RoleTypeDef role,
  AuthService_TokenPairTypeDef* tokens
) {
  uint8_t access[AUTH_SERVICE_TOKEN_BYTES];
  uint8_t refresh[AUTH_SERVICE_TOKEN_BYTES];
  uint8_t accessDigest[32];
  uint8_t refreshDigest[32];
  if ((TlsPlatform_Random(access, sizeof(access)) != HAL_OK)
      || (TlsPlatform_Random(refresh, sizeof(refresh)) != HAL_OK)) {
    return AUTH_SERVICE_STATUS_CRYPTO_ERROR;
  }
  authService_Hex(access, sizeof(access), tokens->accessToken);
  authService_Hex(refresh, sizeof(refresh), tokens->refreshToken);
  mbedtls_platform_zeroize(access, sizeof(access));
  mbedtls_platform_zeroize(refresh, sizeof(refresh));
  if ((authService_HashToken(tokens->accessToken, accessDigest) != HAL_OK)
      || (authService_HashToken(tokens->refreshToken, refreshDigest) != HAL_OK)) {
    mbedtls_platform_zeroize(tokens, sizeof(*tokens));
    return AUTH_SERVICE_STATUS_CRYPTO_ERROR;
  }

  AuthService_SessionTypeDef* session = authService_FindSession(username);
  if (session == NULL) {
    mbedtls_platform_zeroize(tokens, sizeof(*tokens));
    return AUTH_SERVICE_STATUS_CRYPTO_ERROR;
  }
  memset(session, 0, sizeof(*session));
  session->active = 1U;
  session->role = (uint8_t)role;
  (void)strncpy(
    session->username,
    username,
    sizeof(session->username) - 1U
  );
  memcpy(session->accessDigest, accessDigest, sizeof(accessDigest));
  memcpy(session->refreshDigest, refreshDigest, sizeof(refreshDigest));
  session->accessIssuedAt = HAL_GetTick();
  session->refreshIssuedAt = session->accessIssuedAt;
  tokens->accessExpiresIn = AUTH_SERVICE_ACCESS_LIFETIME_SEC;
  tokens->refreshExpiresIn = AUTH_SERVICE_REFRESH_LIFETIME_SEC;
  mbedtls_platform_zeroize(accessDigest, sizeof(accessDigest));
  mbedtls_platform_zeroize(refreshDigest, sizeof(refreshDigest));
  return AUTH_SERVICE_STATUS_OK;
}

AuthService_StatusTypeDef AuthService_Init(void) {
  sessionMutex = xSemaphoreCreateMutexStatic(&sessionMutexControlBlock);
  if ((sessionMutex == NULL) || (UserStore_Init() != HAL_OK))
    return AUTH_SERVICE_STATUS_STORAGE_ERROR;
  memset(sessions, 0, sizeof(sessions));
  return AUTH_SERVICE_STATUS_OK;
}

AuthService_StatusTypeDef AuthService_VerifyMasterPassword(
  const uint8_t* password,
  size_t passwordLength
) {
  if ((password == NULL)
      || (passwordLength == 0U)
      || (passwordLength > AUTH_SERVICE_MAX_PASSWORD_LENGTH)) {
    return AUTH_SERVICE_STATUS_INVALID_ARGUMENT;
  }
  uint8_t derivedVerifier[MASTER_PASSWORD_VERIFIER_SIZE];
  AuthService_StatusTypeDef status = authService_Derive(
    password,
    passwordLength,
    masterPasswordSalt,
    MASTER_PASSWORD_PBKDF2_ITERATIONS,
    derivedVerifier
  );
  if (status == AUTH_SERVICE_STATUS_OK) {
    status = (mbedtls_ct_memcmp(
      derivedVerifier,
      masterPasswordVerifier,
      sizeof(derivedVerifier)
    ) == 0)
      ? AUTH_SERVICE_STATUS_OK
      : AUTH_SERVICE_STATUS_INVALID_PASSWORD;
  }
  mbedtls_platform_zeroize(derivedVerifier, sizeof(derivedVerifier));
  return status;
}

AuthService_StatusTypeDef AuthService_Login(
  const char* username,
  const uint8_t* password,
  size_t passwordLength,
  AuthService_TokenPairTypeDef* tokens
) {
  if ((username == NULL) || (password == NULL) || (tokens == NULL))
    return AUTH_SERVICE_STATUS_INVALID_ARGUMENT;
  UserStore_RoleTypeDef role = USER_ROLE_ADMINISTRATOR;
  AuthService_StatusTypeDef status;
  if (strcmp(username, AUTH_SERVICE_MASTER_USERNAME) == 0) {
    status = AuthService_VerifyMasterPassword(password, passwordLength);
  } else {
    UserStore_RecordTypeDef record;
    if ((UserStore_Find(username, &record, NULL) != HAL_OK)
        || (record.enabled == 0U)) {
      return AUTH_SERVICE_STATUS_INVALID_PASSWORD;
    }
    uint8_t verifier[USER_STORE_VERIFIER_SIZE];
    status = authService_Derive(
      password,
      passwordLength,
      record.salt,
      record.iterations,
      verifier
    );
    if (status == AUTH_SERVICE_STATUS_OK) {
      status = (mbedtls_ct_memcmp(
        verifier,
        record.verifier,
        sizeof(verifier)
      ) == 0)
        ? AUTH_SERVICE_STATUS_OK
        : AUTH_SERVICE_STATUS_INVALID_PASSWORD;
    }
    mbedtls_platform_zeroize(verifier, sizeof(verifier));
    role = (UserStore_RoleTypeDef)record.role;
  }
  if (status != AUTH_SERVICE_STATUS_OK)
    return status;
  if (xSemaphoreTake(sessionMutex, portMAX_DELAY) != pdTRUE)
    return AUTH_SERVICE_STATUS_CRYPTO_ERROR;
  status = authService_Issue(username, role, tokens);
  (void)xSemaphoreGive(sessionMutex);
  return status;
}

AuthService_StatusTypeDef AuthService_Refresh(
  const char* refreshToken,
  AuthService_TokenPairTypeDef* tokens
) {
  if ((refreshToken == NULL)
      || (strlen(refreshToken) != (AUTH_SERVICE_TOKEN_TEXT_SIZE - 1U))
      || (tokens == NULL)) {
    return AUTH_SERVICE_STATUS_INVALID_ARGUMENT;
  }
  uint8_t digest[32];
  if (authService_HashToken(refreshToken, digest) != HAL_OK)
    return AUTH_SERVICE_STATUS_CRYPTO_ERROR;
  if (xSemaphoreTake(sessionMutex, portMAX_DELAY) != pdTRUE)
    return AUTH_SERVICE_STATUS_CRYPTO_ERROR;
  AuthService_StatusTypeDef status = AUTH_SERVICE_STATUS_INVALID_PASSWORD;
  uint32_t now = HAL_GetTick();
  for (size_t index = 0U; index < AUTH_SERVICE_SESSION_COUNT; ++index) {
    AuthService_SessionTypeDef* session = &sessions[index];
    if ((session->active != 0U)
        && ((now - session->refreshIssuedAt)
          <= (AUTH_SERVICE_REFRESH_LIFETIME_SEC * 1000U))
        && (mbedtls_ct_memcmp(
          digest, session->refreshDigest, sizeof(digest)
        ) == 0)) {
      char username[USER_STORE_USERNAME_SIZE];
      (void)strncpy(username, session->username, sizeof(username));
      username[sizeof(username) - 1U] = '\0';
      status = authService_Issue(
        username,
        (UserStore_RoleTypeDef)session->role,
        tokens
      );
      break;
    }
  }
  (void)xSemaphoreGive(sessionMutex);
  mbedtls_platform_zeroize(digest, sizeof(digest));
  return status;
}

AuthService_StatusTypeDef AuthService_Authorize(
  const char* accessToken,
  AuthService_PrincipalTypeDef* principal
) {
  if ((accessToken == NULL)
      || (strlen(accessToken) != (AUTH_SERVICE_TOKEN_TEXT_SIZE - 1U))
      || (principal == NULL)) {
    return AUTH_SERVICE_STATUS_INVALID_ARGUMENT;
  }
  uint8_t digest[32];
  if (authService_HashToken(accessToken, digest) != HAL_OK)
    return AUTH_SERVICE_STATUS_CRYPTO_ERROR;
  if (xSemaphoreTake(sessionMutex, portMAX_DELAY) != pdTRUE)
    return AUTH_SERVICE_STATUS_CRYPTO_ERROR;
  AuthService_StatusTypeDef status = AUTH_SERVICE_STATUS_INVALID_PASSWORD;
  uint32_t now = HAL_GetTick();
  for (size_t index = 0U; index < AUTH_SERVICE_SESSION_COUNT; ++index) {
    AuthService_SessionTypeDef* session = &sessions[index];
    if ((session->active != 0U)
        && ((now - session->accessIssuedAt)
          <= (AUTH_SERVICE_ACCESS_LIFETIME_SEC * 1000U))
        && (mbedtls_ct_memcmp(
          digest, session->accessDigest, sizeof(digest)
        ) == 0)) {
      (void)strncpy(
        principal->username,
        session->username,
        sizeof(principal->username)
      );
      principal->username[sizeof(principal->username) - 1U] = '\0';
      principal->role = (UserStore_RoleTypeDef)session->role;
      status = AUTH_SERVICE_STATUS_OK;
      break;
    }
  }
  (void)xSemaphoreGive(sessionMutex);
  mbedtls_platform_zeroize(digest, sizeof(digest));
  return status;
}

AuthService_StatusTypeDef AuthService_Revoke(const char* accessToken) {
  AuthService_PrincipalTypeDef principal;
  AuthService_StatusTypeDef status = AuthService_Authorize(
    accessToken, &principal
  );
  if (status != AUTH_SERVICE_STATUS_OK)
    return status;
  if (xSemaphoreTake(sessionMutex, portMAX_DELAY) != pdTRUE)
    return AUTH_SERVICE_STATUS_CRYPTO_ERROR;
  AuthService_SessionTypeDef* session = authService_FindSession(
    principal.username
  );
  if (session != NULL)
    mbedtls_platform_zeroize(session, sizeof(*session));
  (void)xSemaphoreGive(sessionMutex);
  return AUTH_SERVICE_STATUS_OK;
}

AuthService_StatusTypeDef AuthService_PutUser(
  const AuthService_PrincipalTypeDef* actor,
  const char* username,
  const uint8_t* password,
  size_t passwordLength,
  UserStore_RoleTypeDef role,
  uint8_t enabled,
  uint8_t mustExist
) {
  if ((authService_IsAdministrator(actor) == 0U)
      || (username == NULL)
      || (password == NULL)
      || (passwordLength < AUTH_SERVICE_MIN_PASSWORD_LENGTH)
      || (passwordLength > AUTH_SERVICE_MAX_PASSWORD_LENGTH)
      || (strlen(username) >= USER_STORE_USERNAME_SIZE)
      || (strcmp(username, AUTH_SERVICE_MASTER_USERNAME) == 0)) {
    return AUTH_SERVICE_STATUS_FORBIDDEN;
  }
  UserStore_RecordTypeDef record;
  uint8_t exists = (UserStore_Find(username, &record, NULL) == HAL_OK);
  if ((mustExist != 0U) && (exists == 0U))
    return AUTH_SERVICE_STATUS_NOT_FOUND;
  if ((mustExist == 0U) && (exists != 0U))
    return AUTH_SERVICE_STATUS_INVALID_ARGUMENT;
  memset(&record, 0, sizeof(record));
  record.enabled = enabled ? 1U : 0U;
  record.role = (uint8_t)role;
  record.iterations = AUTH_SERVICE_USER_ITERATIONS;
  (void)strncpy(record.username, username, sizeof(record.username) - 1U);
  if (TlsPlatform_Random(record.salt, sizeof(record.salt)) != HAL_OK)
    return AUTH_SERVICE_STATUS_CRYPTO_ERROR;
  AuthService_StatusTypeDef status = authService_Derive(
    password,
    passwordLength,
    record.salt,
    record.iterations,
    record.verifier
  );
  if (status != AUTH_SERVICE_STATUS_OK)
    return status;
  status = (UserStore_Put(&record) == HAL_OK)
    ? AUTH_SERVICE_STATUS_OK
    : AUTH_SERVICE_STATUS_STORAGE_ERROR;
  if ((status == AUTH_SERVICE_STATUS_OK)
      && (xSemaphoreTake(sessionMutex, portMAX_DELAY) == pdTRUE)) {
    AuthService_SessionTypeDef* session = authService_FindSession(username);
    if ((session != NULL) && (session->active != 0U)
        && (strcmp(session->username, username) == 0)) {
      mbedtls_platform_zeroize(session, sizeof(*session));
    }
    (void)xSemaphoreGive(sessionMutex);
  }
  mbedtls_platform_zeroize(&record, sizeof(record));
  return status;
}

AuthService_StatusTypeDef AuthService_DeleteUser(
  const AuthService_PrincipalTypeDef* actor,
  const char* username
) {
  if ((authService_IsAdministrator(actor) == 0U)
      || (username == NULL)
      || (strcmp(username, AUTH_SERVICE_MASTER_USERNAME) == 0)) {
    return AUTH_SERVICE_STATUS_FORBIDDEN;
  }
  UserStore_RecordTypeDef record;
  if (UserStore_Find(username, &record, NULL) != HAL_OK)
    return AUTH_SERVICE_STATUS_NOT_FOUND;
  if (UserStore_Delete(username) != HAL_OK)
    return AUTH_SERVICE_STATUS_STORAGE_ERROR;
  if (xSemaphoreTake(sessionMutex, portMAX_DELAY) == pdTRUE) {
    AuthService_SessionTypeDef* session = authService_FindSession(username);
    if ((session != NULL) && (session->active != 0U)
        && (strcmp(session->username, username) == 0)) {
      mbedtls_platform_zeroize(session, sizeof(*session));
    }
    (void)xSemaphoreGive(sessionMutex);
  }
  return AUTH_SERVICE_STATUS_OK;
}

size_t AuthService_ListUsers(
  const AuthService_PrincipalTypeDef* actor,
  UserStore_RecordTypeDef* records,
  size_t capacity
) {
  return (authService_IsAdministrator(actor) != 0U)
    ? UserStore_List(records, capacity)
    : 0U;
}
