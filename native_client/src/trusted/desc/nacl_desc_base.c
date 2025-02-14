/*
 * Copyright (c) 2012 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * NaCl Service Runtime.  I/O Descriptor / Handle abstraction.  Memory
 * mapping using descriptors.
 */

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "native_client/src/include/portability.h"
#include "native_client/src/include/nacl_platform.h"

#include "native_client/src/shared/platform/nacl_host_desc.h"
#include "native_client/src/shared/platform/nacl_log.h"
#include "native_client/src/shared/platform/nacl_sync_checked.h"

#if NACL_LINUX
#include "native_client/src/trusted/desc/linux/nacl_desc_sysv_shm.h"
#endif  /* NACL_LINUX */
#include "native_client/src/trusted/desc/nacl_desc_base.h"
#include "native_client/src/trusted/desc/nacl_desc_cond.h"
#include "native_client/src/trusted/desc/nacl_desc_conn_cap.h"
#include "native_client/src/trusted/desc/nacl_desc_dir.h"
#include "native_client/src/trusted/desc/nacl_desc_imc.h"
#include "native_client/src/trusted/desc/nacl_desc_imc_bound_desc.h"
#include "native_client/src/trusted/desc/nacl_desc_imc_shm.h"
#include "native_client/src/trusted/desc/nacl_desc_invalid.h"
#include "native_client/src/trusted/desc/nacl_desc_io.h"
#include "native_client/src/trusted/desc/nacl_desc_mutex.h"
#include "native_client/src/trusted/desc/nacl_desc_null.h"
#include "native_client/src/trusted/desc/nacl_desc_rng.h"
#include "native_client/src/trusted/desc/nacl_desc_quota.h"
#include "native_client/src/trusted/desc/nacl_desc_sync_socket.h"

#include "native_client/src/trusted/nacl_base/nacl_refcount.h"

#include "native_client/src/trusted/service_runtime/include/bits/mman.h"
#include "native_client/src/trusted/service_runtime/include/sys/errno.h"
#include "native_client/src/trusted/service_runtime/include/sys/stat.h"
#include "native_client/src/trusted/service_runtime/nacl_config.h"
#include "native_client/src/trusted/service_runtime/sel_util.h"

/*
 * This file contains base class code for NaClDesc.
 *
 * The implementation for following subclasses are elsewhere, but here
 * is an enumeration of them with a brief description:
 *
 * NaClDescIoDesc is the subclass that wraps host-OS descriptors
 * provided by NaClHostDesc (which gives an OS-independent abstraction
 * for host-OS descriptors).
 *
 * NaClDescImcDesc is the subclass that wraps IMC descriptors.
 *
 * NaClDescMutex and NaClDescCondVar are the subclasses that
 * wrap the non-transferrable synchronization objects.
 *
 * These NaClDesc objects are impure in that they know about the
 * virtual memory subsystem restriction of requiring mappings to occur
 * in NACL_MAP_PAGESIZE (64KB) chunks, so the Map and Unmap virtual
 * functions, at least, will enforce this restriction.
 */

/*
 * TODO(bsy): remove when we put SIZE_T_MAX in a common header file.
 */
#if !defined(SIZE_T_MAX)
# define SIZE_T_MAX ((size_t) -1)
#endif

int NaClDescCtor(struct NaClDesc *ndp) {
  /* this should be a compile-time test */
  if (0 != (sizeof(struct NaClInternalHeader) & 0xf)) {
    NaClLog(LOG_FATAL,
            "Internal error.  NaClInternalHeader size not a"
            " multiple of 16\n");
  }
  return NaClRefCountCtor(&ndp->base);
}

static void NaClDescDtor(struct NaClRefCount *nrcp) {
  nrcp->vtbl = &kNaClRefCountVtbl;
  (*nrcp->vtbl->Dtor)(nrcp);
}

struct NaClDesc *NaClDescRef(struct NaClDesc *ndp) {
  return (struct NaClDesc *) NaClRefCountRef(&ndp->base);
}

void NaClDescUnref(struct NaClDesc *ndp) {
  NaClRefCountUnref(&ndp->base);
}

void NaClDescSafeUnref(struct NaClDesc *ndp) {
  if (NULL != ndp) {
    NaClRefCountUnref(&ndp->base);
  }
}

int (*NaClDescInternalize[NACL_DESC_TYPE_MAX])(
    struct NaClDesc **,
    struct NaClDescXferState *,
    struct NaClDescQuotaInterface *) = {
  NaClDescInvalidInternalize,
  NaClDescDirInternalize,
  NaClDescIoInternalize,
#if NACL_WINDOWS
  NaClDescConnCapInternalize,
  NaClDescInternalizeNotImplemented,
#else
  NaClDescInternalizeNotImplemented,
  NaClDescConnCapFdInternalize,
#endif
  NaClDescInternalizeNotImplemented,  /* bound sockets cannot be transferred */
  NaClDescInternalizeNotImplemented,  /* connected abstract base class */
  NaClDescImcShmInternalize,
#if NACL_LINUX && !NACL_ANDROID
  NaClDescSysvShmInternalize,
#else
  NULL,
#endif  /* NACL_LINUX */
  NaClDescInternalizeNotImplemented,  /* mutex */
  NaClDescInternalizeNotImplemented,  /* condvar */
  NaClDescInternalizeNotImplemented,  /* semaphore */
  NaClDescSyncSocketInternalize,
  NaClDescXferableDataDescInternalize,
  NaClDescInternalizeNotImplemented,  /* imc socket */
  NaClDescQuotaInternalize,           /* quota wrapper */
  NaClDescRngInternalize,             /* device: rng */
  NaClDescInternalizeNotImplemented,  /* device: postmessage */
  NaClDescInternalizeNotImplemented,  /* custom */
  NaClDescNullInternalize,
};

char const *NaClDescTypeString(enum NaClDescTypeTag type_tag) {
  /* default functions for the vtable - return NOT_IMPLEMENTED */
  switch (type_tag) {
#define MAP(E) case E: do { return #E; } while (0)
    MAP(NACL_DESC_INVALID);
    MAP(NACL_DESC_DIR);
    MAP(NACL_DESC_HOST_IO);
    MAP(NACL_DESC_CONN_CAP);
    MAP(NACL_DESC_CONN_CAP_FD);
    MAP(NACL_DESC_BOUND_SOCKET);
    MAP(NACL_DESC_CONNECTED_SOCKET);
    MAP(NACL_DESC_SHM);
    MAP(NACL_DESC_SYSV_SHM);
    MAP(NACL_DESC_MUTEX);
    MAP(NACL_DESC_CONDVAR);
    MAP(NACL_DESC_SEMAPHORE);
    MAP(NACL_DESC_SYNC_SOCKET);
    MAP(NACL_DESC_TRANSFERABLE_DATA_SOCKET);
    MAP(NACL_DESC_IMC_SOCKET);
    MAP(NACL_DESC_QUOTA);
    MAP(NACL_DESC_DEVICE_RNG);
    MAP(NACL_DESC_DEVICE_POSTMESSAGE);
    MAP(NACL_DESC_CUSTOM);
    MAP(NACL_DESC_NULL);
  }
  return "BAD TYPE TAG";
}


void NaClDescDtorNotImplemented(struct NaClRefCount  *vself) {
  UNREFERENCED_PARAMETER(vself);

  NaClLog(LOG_FATAL, "Must implement a destructor!\n");
}

uintptr_t NaClDescMapNotImplemented(struct NaClDesc         *vself,
                                    struct NaClDescEffector *effp,
                                    void                    *start_addr,
                                    size_t                  len,
                                    int                     prot,
                                    int                     flags,
                                    nacl_off64_t            offset) {
  UNREFERENCED_PARAMETER(effp);
  UNREFERENCED_PARAMETER(start_addr);
  UNREFERENCED_PARAMETER(len);
  UNREFERENCED_PARAMETER(prot);
  UNREFERENCED_PARAMETER(flags);
  UNREFERENCED_PARAMETER(offset);

  NaClLog(LOG_ERROR,
          "Map method is not implemented for object of type %s\n",
          NaClDescTypeString(((struct NaClDescVtbl const *)
                              vself->base.vtbl)->typeTag));
  return -NACL_ABI_EINVAL;
}

#if NACL_WINDOWS
int NaClDescUnmapUnsafeNotImplemented(struct NaClDesc         *vself,
                                      void                    *start_addr,
                                      size_t                  len) {
  UNREFERENCED_PARAMETER(start_addr);
  UNREFERENCED_PARAMETER(len);

  NaClLog(LOG_ERROR,
          "Map method is not implemented for object of type %s\n",
          NaClDescTypeString(((struct NaClDescVtbl const *)
                              vself->base.vtbl)->typeTag));
  return -NACL_ABI_EINVAL;
}
#endif

ssize_t NaClDescReadNotImplemented(struct NaClDesc          *vself,
                                   void                     *buf,
                                   size_t                   len) {
  UNREFERENCED_PARAMETER(buf);
  UNREFERENCED_PARAMETER(len);

  NaClLog(LOG_ERROR,
          "Read method is not implemented for object of type %s\n",
          NaClDescTypeString(((struct NaClDescVtbl const *)
                              vself->base.vtbl)->typeTag));
  return -NACL_ABI_EINVAL;
}

ssize_t NaClDescWriteNotImplemented(struct NaClDesc         *vself,
                                    void const              *buf,
                                    size_t                  len) {
  UNREFERENCED_PARAMETER(buf);
  UNREFERENCED_PARAMETER(len);

  NaClLog(LOG_ERROR,
          "Write method is not implemented for object of type %s\n",
          NaClDescTypeString(((struct NaClDescVtbl const *)
                              vself->base.vtbl)->typeTag));
  return -NACL_ABI_EINVAL;
}

nacl_off64_t NaClDescSeekNotImplemented(struct NaClDesc          *vself,
                                        nacl_off64_t             offset,
                                        int                      whence) {
  UNREFERENCED_PARAMETER(offset);
  UNREFERENCED_PARAMETER(whence);

  NaClLog(LOG_ERROR,
          "Seek method is not implemented for object of type %s\n",
          NaClDescTypeString(((struct NaClDescVtbl const *)
                              vself->base.vtbl)->typeTag));
  return -NACL_ABI_EINVAL;
}

int NaClDescIoctlNotImplemented(struct NaClDesc         *vself,
                                int                     request,
                                void                    *arg) {
  UNREFERENCED_PARAMETER(request);
  UNREFERENCED_PARAMETER(arg);

  NaClLog(LOG_ERROR,
          "Ioctl method is not implemented for object of type %s\n",
          NaClDescTypeString(((struct NaClDescVtbl const *)
                              vself->base.vtbl)->typeTag));
  return -NACL_ABI_EINVAL;
}

int NaClDescFstatNotImplemented(struct NaClDesc         *vself,
                                struct nacl_abi_stat    *statbuf) {
  UNREFERENCED_PARAMETER(statbuf);

  NaClLog(LOG_ERROR,
          "Fstat method is not implemented for object of type %s\n",
          NaClDescTypeString(((struct NaClDescVtbl const *)
                              vself->base.vtbl)->typeTag));
  return -NACL_ABI_EINVAL;
}

ssize_t NaClDescGetdentsNotImplemented(struct NaClDesc          *vself,
                                       void                     *dirp,
                                       size_t                   count) {
  UNREFERENCED_PARAMETER(dirp);
  UNREFERENCED_PARAMETER(count);

  NaClLog(LOG_ERROR,
          "Getdents method is not implemented for object of type %s\n",
          NaClDescTypeString(((struct NaClDescVtbl const *)
                              vself->base.vtbl)->typeTag));
  return -NACL_ABI_EINVAL;
}

int NaClDescExternalizeSizeNotImplemented(struct NaClDesc *vself,
                                          size_t          *nbytes,
                                          size_t          *nhandles) {
  UNREFERENCED_PARAMETER(nbytes);
  UNREFERENCED_PARAMETER(nhandles);

  NaClLog(LOG_ERROR,
          "ExternalizeSize method is not implemented for object of type %s\n",
          NaClDescTypeString(((struct NaClDescVtbl const *)
                              vself->base.vtbl)->typeTag));
  return -NACL_ABI_EINVAL;
}

int NaClDescExternalizeNotImplemented(struct NaClDesc          *vself,
                                      struct NaClDescXferState *xfer) {
  UNREFERENCED_PARAMETER(xfer);

  NaClLog(LOG_ERROR,
          "Externalize method is not implemented for object of type %s\n",
          NaClDescTypeString(((struct NaClDescVtbl const *)
                              vself->base.vtbl)->typeTag));
  return -NACL_ABI_EINVAL;
}

int NaClDescLockNotImplemented(struct NaClDesc  *vself) {
  NaClLog(LOG_ERROR,
          "Lock method is not implemented for object of type %s\n",
          NaClDescTypeString(((struct NaClDescVtbl const *)
                              vself->base.vtbl)->typeTag));
  return -NACL_ABI_EINVAL;
}

int NaClDescTryLockNotImplemented(struct NaClDesc *vself) {
  NaClLog(LOG_ERROR,
          "TryLock method is not implemented for object of type %s\n",
          NaClDescTypeString(((struct NaClDescVtbl const *)
                              vself->base.vtbl)->typeTag));
  return -NACL_ABI_EINVAL;
}

int NaClDescUnlockNotImplemented(struct NaClDesc  *vself) {
  NaClLog(LOG_ERROR,
          "Unlock method is not implemented for object of type %s\n",
          NaClDescTypeString(((struct NaClDescVtbl const *)
                              vself->base.vtbl)->typeTag));
  return -NACL_ABI_EINVAL;
}

int NaClDescWaitNotImplemented(struct NaClDesc  *vself,
                               struct NaClDesc  *mutex) {
  UNREFERENCED_PARAMETER(mutex);

  NaClLog(LOG_ERROR,
          "Wait method is not implemented for object of type %s\n",
          NaClDescTypeString(((struct NaClDescVtbl const *)
                              vself->base.vtbl)->typeTag));
  return -NACL_ABI_EINVAL;
}

int NaClDescTimedWaitAbsNotImplemented(struct NaClDesc                *vself,
                                       struct NaClDesc                *mutex,
                                       struct nacl_abi_timespec const *ts) {
  UNREFERENCED_PARAMETER(mutex);
  UNREFERENCED_PARAMETER(ts);

  NaClLog(LOG_ERROR,
          "TimedWaitAbs method is not implemented for object of type %s\n",
          NaClDescTypeString(((struct NaClDescVtbl const *)
                              vself->base.vtbl)->typeTag));
  return -NACL_ABI_EINVAL;
}

int NaClDescSignalNotImplemented(struct NaClDesc  *vself) {
  NaClLog(LOG_ERROR,
          "Signal method is not implemented for object of type %s\n",
          NaClDescTypeString(((struct NaClDescVtbl const *)
                              vself->base.vtbl)->typeTag));
  return -NACL_ABI_EINVAL;
}

int NaClDescBroadcastNotImplemented(struct NaClDesc *vself) {
  NaClLog(LOG_ERROR,
          "Broadcast method is not implemented for object of type %s\n",
          NaClDescTypeString(((struct NaClDescVtbl const *)
                              vself->base.vtbl)->typeTag));
  return -NACL_ABI_EINVAL;
}

ssize_t NaClDescSendMsgNotImplemented(
    struct NaClDesc                 *vself,
    const struct NaClImcTypedMsgHdr *nitmhp,
    int                             flags) {
  UNREFERENCED_PARAMETER(nitmhp);
  UNREFERENCED_PARAMETER(flags);

  NaClLog(LOG_ERROR,
          "SendMsg method is not implemented for object of type %s\n",
          NaClDescTypeString(((struct NaClDescVtbl const *)
                              vself->base.vtbl)->typeTag));
  return -NACL_ABI_EINVAL;
}

ssize_t NaClDescRecvMsgNotImplemented(
    struct NaClDesc                 *vself,
    struct NaClImcTypedMsgHdr       *nitmhp,
    int                             flags,
    struct NaClDescQuotaInterface   *quota_interface) {
  UNREFERENCED_PARAMETER(nitmhp);
  UNREFERENCED_PARAMETER(flags);
  UNREFERENCED_PARAMETER(quota_interface);

  NaClLog(LOG_ERROR,
          "RecvMsg method is not implemented for object of type %s\n",
          NaClDescTypeString(((struct NaClDescVtbl const *)
                              vself->base.vtbl)->typeTag));
  return -NACL_ABI_EINVAL;
}

ssize_t NaClDescLowLevelSendMsgNotImplemented(
    struct NaClDesc                *vself,
    struct NaClMessageHeader const *dgram,
    int                            flags) {
  UNREFERENCED_PARAMETER(dgram);
  UNREFERENCED_PARAMETER(flags);

  NaClLog(LOG_ERROR,
          "LowLevelSendMsg method is not implemented for object of type %s\n",
          NaClDescTypeString(((struct NaClDescVtbl const *)
                              vself->base.vtbl)->typeTag));
  return -NACL_ABI_EINVAL;
}

ssize_t NaClDescLowLevelRecvMsgNotImplemented(
    struct NaClDesc           *vself,
    struct NaClMessageHeader  *dgram,
    int                       flags) {
  UNREFERENCED_PARAMETER(dgram);
  UNREFERENCED_PARAMETER(flags);

  NaClLog(LOG_ERROR,
          "LowLevelRecvMsg method is not implemented for object of type %s\n",
          NaClDescTypeString(((struct NaClDescVtbl const *)
                              vself->base.vtbl)->typeTag));
  return -NACL_ABI_EINVAL;
}

int NaClDescConnectAddrNotImplemented(struct NaClDesc *vself,
                                      struct NaClDesc **result) {
  UNREFERENCED_PARAMETER(result);

  NaClLog(LOG_ERROR,
          "ConnectAddr method is not implemented for object of type %s\n",
          NaClDescTypeString(((struct NaClDescVtbl const *)
                              vself->base.vtbl)->typeTag));
  return -NACL_ABI_EINVAL;
}

int NaClDescAcceptConnNotImplemented(struct NaClDesc *vself,
                                     struct NaClDesc **result) {
  UNREFERENCED_PARAMETER(result);

  NaClLog(LOG_ERROR,
          "AcceptConn method is not implemented for object of type %s\n",
          NaClDescTypeString(((struct NaClDescVtbl const *)
                              vself->base.vtbl)->typeTag));
  return -NACL_ABI_EINVAL;
}

int NaClDescPostNotImplemented(struct NaClDesc  *vself) {
  NaClLog(LOG_ERROR,
          "Post method is not implemented for object of type %s\n",
          NaClDescTypeString(((struct NaClDescVtbl const *)
                              vself->base.vtbl)->typeTag));
  return -NACL_ABI_EINVAL;
}

int NaClDescSemWaitNotImplemented(struct NaClDesc *vself) {
  NaClLog(LOG_ERROR,
          "SemWait method is not implemented for object of type %s\n",
          NaClDescTypeString(((struct NaClDescVtbl const *)
                              vself->base.vtbl)->typeTag));
  return -NACL_ABI_EINVAL;
}

int NaClDescGetValueNotImplemented(struct NaClDesc  *vself) {
  NaClLog(LOG_ERROR,
          "GetValue method is not implemented for object of type %s\n",
          NaClDescTypeString(((struct NaClDescVtbl const *)
                              vself->base.vtbl)->typeTag));
  return -NACL_ABI_EINVAL;
}

int NaClDescInternalizeNotImplemented(
    struct NaClDesc                **out_desc,
    struct NaClDescXferState       *xfer,
    struct NaClDescQuotaInterface  *quota_interface) {
  UNREFERENCED_PARAMETER(out_desc);
  UNREFERENCED_PARAMETER(xfer);
  UNREFERENCED_PARAMETER(quota_interface);

  NaClLog(LOG_ERROR,
          "Attempted transfer of non-transferable descriptor\n");
  return -NACL_ABI_EIO;
}

int NaClSafeCloseNaClHandle(NaClHandle h) {
  if (NACL_INVALID_HANDLE != h) {
    return NaClClose(h);
  }
  return 0;
}


struct NaClDescVtbl const kNaClDescVtbl = {
  {
    NaClDescDtor,
  },
  NaClDescMapNotImplemented,
  NACL_DESC_UNMAP_NOT_IMPLEMENTED
  NaClDescReadNotImplemented,
  NaClDescWriteNotImplemented,
  NaClDescSeekNotImplemented,
  NaClDescIoctlNotImplemented,
  NaClDescFstatNotImplemented,
  NaClDescGetdentsNotImplemented,
  (enum NaClDescTypeTag) -1,  /* NaClDesc is an abstract base class */
  NaClDescExternalizeSizeNotImplemented,
  NaClDescExternalizeNotImplemented,
  NaClDescLockNotImplemented,
  NaClDescTryLockNotImplemented,
  NaClDescUnlockNotImplemented,
  NaClDescWaitNotImplemented,
  NaClDescTimedWaitAbsNotImplemented,
  NaClDescSignalNotImplemented,
  NaClDescBroadcastNotImplemented,
  NaClDescSendMsgNotImplemented,
  NaClDescRecvMsgNotImplemented,
  NaClDescLowLevelSendMsgNotImplemented,
  NaClDescLowLevelRecvMsgNotImplemented,
  NaClDescConnectAddrNotImplemented,
  NaClDescAcceptConnNotImplemented,
  NaClDescPostNotImplemented,
  NaClDescSemWaitNotImplemented,
  NaClDescGetValueNotImplemented,
};
