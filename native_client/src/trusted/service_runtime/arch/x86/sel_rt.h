/*
 * Copyright (c) 2011 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * NaCl Secure Runtime
 */

#ifndef __NATIVE_CLIENT_SERVICE_RUNTIME_ARCH_X86_SEL_RT_H__
#define __NATIVE_CLIENT_SERVICE_RUNTIME_ARCH_X86_SEL_RT_H__ 1

#include "native_client/src/include/portability.h"

#if NACL_BUILD_SUBARCH == 32
# include "native_client/src/trusted/service_runtime/arch/x86_32/sel_rt_32.h"
#elif NACL_BUILD_SUBARCH == 64
# include "native_client/src/trusted/service_runtime/arch/x86_64/sel_rt_64.h"
#else
# error "Woe to the service runtime.  Is it running on a 128-bit machine?!?"
#endif

nacl_reg_t NaClGetStackPtr(void);

/*
 * This is the default state of the x87 FPU control word,
 * as set by the "fninit" instruction.
 */
#define NACL_X87_FCW_DEFAULT    (0x37f)

#endif /* __NATIVE_CLIENT_SERVICE_RUNTIME_ARCH_X86_SEL_RT_H__ */
