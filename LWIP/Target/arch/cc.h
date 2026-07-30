/*
 * Copyright (c) 2001-2003 Swedish Institute of Computer Science.
 * Copyright (c) 2017-2026 Dmitry Slobodchikov.
 * All rights reserved.
 *
 * This file provides the compiler adaptation required by lwIP.
 */

#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

#include "cpu.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

typedef int sys_prot_t;

#define LWIP_ERRNO_STDINCLUDE
#define LWIP_TIMEVAL_PRIVATE 0

#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_STRUCT __attribute__((__packed__))
#define PACK_STRUCT_END
#define PACK_STRUCT_FIELD(x) x

#define LWIP_PLATFORM_ASSERT(message) do { \
  printf( \
    "Assertion \"%s\" failed at line %d in %s\n", \
    (message), \
    __LINE__, \
    __FILE__ \
  ); \
} while (0)

/**
  * @brief Return a PRNG value while excluding lwIP's TCP port sentinel.
  * @return Hardware-seeded pseudo-random value.
  */
static inline uint32_t lwipArch_Random(void) {
  uint32_t value = (uint32_t)rand();
  if ((value & 0x3FFFU) == 0x3FFFU)
    value ^= 1U;
  return value;
}

#define LWIP_RAND() lwipArch_Random()

#endif /* LWIP_ARCH_CC_H */
