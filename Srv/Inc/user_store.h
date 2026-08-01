/**
  ******************************************************************************
  * @file           : user_store.h
  * @brief          : Persistent management-user database.
  * @project        : STM32F407 Health Check
  * @platform       : STMicroelectronics STM32F407VET6
  * @created        : 31.07.2026
  ******************************************************************************
  */

#ifndef USER_STORE_H
#define USER_STORE_H

#include "main.h"

#include <stddef.h>
#include <stdint.h>

#define USER_STORE_MAX_USERS       8U
#define USER_STORE_USERNAME_SIZE   25U
#define USER_STORE_SALT_SIZE       16U
#define USER_STORE_VERIFIER_SIZE   32U

typedef enum {
  USER_ROLE_USER = 0,
  USER_ROLE_ADMINISTRATOR
} UserStore_RoleTypeDef;

typedef struct {
  uint8_t enabled;
  uint8_t role;
  char username[USER_STORE_USERNAME_SIZE];
  uint8_t salt[USER_STORE_SALT_SIZE];
  uint8_t verifier[USER_STORE_VERIFIER_SIZE];
  uint32_t iterations;
} UserStore_RecordTypeDef;

HAL_StatusTypeDef UserStore_Init(void);
HAL_StatusTypeDef UserStore_Find(
  const char* username,
  UserStore_RecordTypeDef* record,
  uint8_t* index
);
HAL_StatusTypeDef UserStore_Put(const UserStore_RecordTypeDef* record);
HAL_StatusTypeDef UserStore_Delete(const char* username);
size_t UserStore_List(UserStore_RecordTypeDef* records, size_t capacity);

#endif /* USER_STORE_H */
