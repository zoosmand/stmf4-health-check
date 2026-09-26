/**
  ******************************************************************************
  * @file           : auth_service.h
  * @brief          : Master-password verification for the management API.
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

#ifndef AUTH_SERVICE_H
#define AUTH_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#include "user_store.h"

#define AUTH_SERVICE_TOKEN_TEXT_SIZE 65U

/**
  * @brief Result of master-password verification.
  */
typedef enum {
  AUTH_SERVICE_STATUS_OK = 0,
  AUTH_SERVICE_STATUS_INVALID_PASSWORD,
  AUTH_SERVICE_STATUS_INVALID_ARGUMENT,
  AUTH_SERVICE_STATUS_CRYPTO_ERROR,
  AUTH_SERVICE_STATUS_NOT_FOUND,
  AUTH_SERVICE_STATUS_FORBIDDEN,
  AUTH_SERVICE_STATUS_STORAGE_ERROR
} AuthService_StatusTypeDef;

/**
  * @brief Newly issued opaque access and refresh credentials.
  * @param accessToken (char[AUTH_SERVICE_TOKEN_TEXT_SIZE]) Access token text.
  * @param refreshToken (char[AUTH_SERVICE_TOKEN_TEXT_SIZE]) Refresh token text.
  * @param accessExpiresIn (uint32_t) Access-token lifetime in seconds.
  * @param refreshExpiresIn (uint32_t) Refresh-token lifetime in seconds.
  */
typedef struct {
  char accessToken[AUTH_SERVICE_TOKEN_TEXT_SIZE];
  char refreshToken[AUTH_SERVICE_TOKEN_TEXT_SIZE];
  uint32_t accessExpiresIn;
  uint32_t refreshExpiresIn;
} AuthService_TokenPairTypeDef;

/** @brief Authenticated username and authorization role. */
typedef struct {
  char username[USER_STORE_USERNAME_SIZE];
  UserStore_RoleTypeDef role;
} AuthService_PrincipalTypeDef;

/** @brief Initialize persistent users and clear all in-memory sessions. */
AuthService_StatusTypeDef AuthService_Init(void);

/**
  * @brief Verify a candidate master password against the Flash-resident
  *        PBKDF2-HMAC-SHA-256 verifier.
  * @param password (const uint8_t*) Non-null password bytes.
  * @param passwordLength (size_t) Password length from 1 through 128 bytes.
  * @retval (AuthService_StatusTypeDef) AUTH_SERVICE_STATUS_OK only when the
  *         supplied password matches.
  * @note This function blocks while deriving the verifier and must run in task
  *       context. It never logs or retains the supplied password.
  */
AuthService_StatusTypeDef AuthService_VerifyMasterPassword(
  const uint8_t* password,
  size_t passwordLength
);

/**
  * @brief Verify credentials and replace the user's active token pair.
  * @param username (const char*) Null-terminated account name.
  * @param password (const uint8_t*) Non-null password bytes.
  * @param passwordLength (size_t) Password length in bytes.
  * @param tokens (AuthService_TokenPairTypeDef*) Issued token output.
  */
AuthService_StatusTypeDef AuthService_Login(
  const char* username,
  const uint8_t* password,
  size_t passwordLength,
  AuthService_TokenPairTypeDef* tokens
);

/**
  * @brief Rotate the session identified by a valid refresh token.
  * @param refreshToken (const char*) Null-terminated 64-character token.
  * @param tokens (AuthService_TokenPairTypeDef*) Rotated token output.
  */
AuthService_StatusTypeDef AuthService_Refresh(
  const char* refreshToken,
  AuthService_TokenPairTypeDef* tokens
);

/**
  * @brief Validate an access token and return its current principal.
  * @param accessToken (const char*) Null-terminated 64-character token.
  * @param principal (AuthService_PrincipalTypeDef*) Authenticated output.
  */
AuthService_StatusTypeDef AuthService_Authorize(
  const char* accessToken,
  AuthService_PrincipalTypeDef* principal
);

/** @brief Revoke the session selected by a valid access token. */
AuthService_StatusTypeDef AuthService_Revoke(const char* accessToken);

/**
  * @brief Create or replace a persistent non-master account.
  * @param actor (const AuthService_PrincipalTypeDef*) Administrator principal.
  * @param username (const char*) Target account name.
  * @param password (const uint8_t*) New password bytes.
  * @param passwordLength (size_t) Password length in bytes.
  * @param role (UserStore_RoleTypeDef) New authorization role.
  * @param enabled (uint8_t) Nonzero to permit login.
  * @param mustExist (uint8_t) Nonzero for update-only behavior.
  */
AuthService_StatusTypeDef AuthService_PutUser(
  const AuthService_PrincipalTypeDef* actor,
  const char* username,
  const uint8_t* password,
  size_t passwordLength,
  UserStore_RoleTypeDef role,
  uint8_t enabled,
  uint8_t mustExist
);

/** @brief Delete a persistent account and revoke its active session. */
AuthService_StatusTypeDef AuthService_DeleteUser(
  const AuthService_PrincipalTypeDef* actor,
  const char* username
);

/**
  * @brief List persistent accounts for an administrator.
  * @param actor (const AuthService_PrincipalTypeDef*) Administrator principal.
  * @param records (UserStore_RecordTypeDef*) Output array.
  * @param capacity (size_t) Available record elements.
  * @retval (size_t) Number of records copied; zero when unauthorized.
  */
size_t AuthService_ListUsers(
  const AuthService_PrincipalTypeDef* actor,
  UserStore_RecordTypeDef* records,
  size_t capacity
);

#endif /* AUTH_SERVICE_H */
