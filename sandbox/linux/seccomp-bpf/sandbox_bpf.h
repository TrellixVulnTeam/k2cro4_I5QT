// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SANDBOX_LINUX_SECCOMP_BPF_SANDBOX_BPF_H__
#define SANDBOX_LINUX_SECCOMP_BPF_SANDBOX_BPF_H__

#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/audit.h>
#include <linux/filter.h>
// #include <linux/seccomp.h>
#include <linux/unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <sched.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <limits>
#include <map>
#include <utility>
#include <vector>

#ifndef SECCOMP_BPF_STANDALONE
#include "base/basictypes.h"
#include "base/logging.h"
#include "base/posix/eintr_wrapper.h"
#endif

#if defined(SECCOMP_BPF_VALGRIND_HACKS)
#ifndef SECCOMP_BPF_STANDALONE
#include "base/third_party/valgrind/valgrind.h"
#endif
#endif


// The Seccomp2 kernel ABI is not part of older versions of glibc.
// As we can't break compilation with these versions of the library,
// we explicitly define all missing symbols.

#ifndef PR_SET_NO_NEW_PRIVS
#define PR_SET_NO_NEW_PRIVS          38
#define PR_GET_NO_NEW_PRIVS          39
#endif
#ifndef IPC_64
#define IPC_64                   0x0100
#endif
#ifndef SECCOMP_MODE_FILTER
#define SECCOMP_MODE_DISABLED         0
#define SECCOMP_MODE_STRICT           1
#define SECCOMP_MODE_FILTER           2  // User user-supplied filter
#define SECCOMP_RET_KILL    0x00000000U  // Kill the task immediately
#define SECCOMP_RET_TRAP    0x00030000U  // Disallow and force a SIGSYS
#define SECCOMP_RET_ERRNO   0x00050000U  // Returns an errno
#define SECCOMP_RET_TRACE   0x7ff00000U  // Pass to a tracer or disallow
#define SECCOMP_RET_ALLOW   0x7fff0000U  // Allow
#define SECCOMP_RET_INVALID 0x8f8f8f8fU  // Illegal return value
#define SECCOMP_RET_ACTION  0xffff0000U  // Masks for the return value
#define SECCOMP_RET_DATA    0x0000ffffU  //   sections
#endif
#define SECCOMP_DENY_ERRNO  EPERM
#ifndef SYS_SECCOMP
#define SYS_SECCOMP                   1
#endif

// Impose some reasonable maximum BPF program size. Realistically, the
// kernel probably has much lower limits. But by limiting to less than
// 30 bits, we can ease requirements on some of our data types.
#define SECCOMP_MAX_PROGRAM_SIZE (1<<30)

#if defined(__i386__)
#define MIN_SYSCALL         0u
#define MAX_PUBLIC_SYSCALL  1024u
#define MAX_SYSCALL         MAX_PUBLIC_SYSCALL
#define SECCOMP_ARCH AUDIT_ARCH_I386

#define SECCOMP_REG(_ctx, _reg) ((_ctx)->uc_mcontext.gregs[(_reg)])
#define SECCOMP_RESULT(_ctx)    SECCOMP_REG(_ctx, REG_EAX)
#define SECCOMP_SYSCALL(_ctx)   SECCOMP_REG(_ctx, REG_EAX)
#define SECCOMP_IP(_ctx)        SECCOMP_REG(_ctx, REG_EIP)
#define SECCOMP_PARM1(_ctx)     SECCOMP_REG(_ctx, REG_EBX)
#define SECCOMP_PARM2(_ctx)     SECCOMP_REG(_ctx, REG_ECX)
#define SECCOMP_PARM3(_ctx)     SECCOMP_REG(_ctx, REG_EDX)
#define SECCOMP_PARM4(_ctx)     SECCOMP_REG(_ctx, REG_ESI)
#define SECCOMP_PARM5(_ctx)     SECCOMP_REG(_ctx, REG_EDI)
#define SECCOMP_PARM6(_ctx)     SECCOMP_REG(_ctx, REG_EBP)

#elif defined(__x86_64__)
#define MIN_SYSCALL         0u
#define MAX_PUBLIC_SYSCALL  1024u
#define MAX_SYSCALL         MAX_PUBLIC_SYSCALL
#define SECCOMP_ARCH AUDIT_ARCH_X86_64

#define SECCOMP_REG(_ctx, _reg) ((_ctx)->uc_mcontext.gregs[(_reg)])
#define SECCOMP_RESULT(_ctx)    SECCOMP_REG(_ctx, REG_RAX)
#define SECCOMP_SYSCALL(_ctx)   SECCOMP_REG(_ctx, REG_RAX)
#define SECCOMP_IP(_ctx)        SECCOMP_REG(_ctx, REG_RIP)
#define SECCOMP_PARM1(_ctx)     SECCOMP_REG(_ctx, REG_RDI)
#define SECCOMP_PARM2(_ctx)     SECCOMP_REG(_ctx, REG_RSI)
#define SECCOMP_PARM3(_ctx)     SECCOMP_REG(_ctx, REG_RDX)
#define SECCOMP_PARM4(_ctx)     SECCOMP_REG(_ctx, REG_R10)
#define SECCOMP_PARM5(_ctx)     SECCOMP_REG(_ctx, REG_R8)
#define SECCOMP_PARM6(_ctx)     SECCOMP_REG(_ctx, REG_R9)

#elif defined(__arm__) && (defined(__thumb__) || defined(__ARM_EABI__))
// ARM EABI includes "ARM private" system calls starting at |__ARM_NR_BASE|,
// and a "ghost syscall private to the kernel", cmpxchg,
// at |__ARM_NR_BASE+0x00fff0|.
// See </arch/arm/include/asm/unistd.h> in the Linux kernel.
#define MIN_SYSCALL         ((unsigned int)__NR_SYSCALL_BASE)
#define MAX_PUBLIC_SYSCALL  (MIN_SYSCALL + 1024u)
#define MIN_PRIVATE_SYSCALL ((unsigned int)__ARM_NR_BASE)
#define MAX_PRIVATE_SYSCALL (MIN_PRIVATE_SYSCALL + 16u)
#define MIN_GHOST_SYSCALL   ((unsigned int)__ARM_NR_BASE + 0xfff0u)
#define MAX_SYSCALL         (MIN_GHOST_SYSCALL + 4u)
// <linux/audit.h> includes <linux/elf-em.h>, which does not define EM_ARM.
// <linux/elf.h> only includes <asm/elf.h> if we're in the kernel.
# if !defined(EM_ARM)
# define EM_ARM 40
# endif
#define SECCOMP_ARCH AUDIT_ARCH_ARM

// ARM sigcontext_t is different from i386/x86_64.
// See </arch/arm/include/asm/sigcontext.h> in the Linux kernel.
#define SECCOMP_REG(_ctx, _reg) ((_ctx)->uc_mcontext.arm_##_reg)
// ARM EABI syscall convention.
#define SECCOMP_RESULT(_ctx)     SECCOMP_REG(_ctx, r0)
#define SECCOMP_SYSCALL(_ctx)    SECCOMP_REG(_ctx, r7)
#define SECCOMP_IP(_ctx)         SECCOMP_REG(_ctx, pc)
#define SECCOMP_PARM1(_ctx)      SECCOMP_REG(_ctx, r0)
#define SECCOMP_PARM2(_ctx)      SECCOMP_REG(_ctx, r1)
#define SECCOMP_PARM3(_ctx)      SECCOMP_REG(_ctx, r2)
#define SECCOMP_PARM4(_ctx)      SECCOMP_REG(_ctx, r3)
#define SECCOMP_PARM5(_ctx)      SECCOMP_REG(_ctx, r4)
#define SECCOMP_PARM6(_ctx)      SECCOMP_REG(_ctx, r5)

#else
#error Unsupported target platform

#endif

#if defined(SECCOMP_BPF_STANDALONE)
#define arraysize(x) (sizeof(x)/sizeof(*(x)))
#define HANDLE_EINTR TEMP_FAILURE_RETRY
#define DISALLOW_IMPLICIT_CONSTRUCTORS(TypeName) \
  TypeName();                                    \
  TypeName(const TypeName&);                     \
  void operator=(const TypeName&)
#endif

#include "sandbox/linux/seccomp-bpf/die.h"
#include "sandbox/linux/seccomp-bpf/errorcode.h"

namespace playground2 {

struct arch_seccomp_data {
  int      nr;
  uint32_t arch;
  uint64_t instruction_pointer;
  uint64_t args[6];
};

struct arch_sigsys {
  void         *ip;
  int          nr;
  unsigned int arch;
};

class CodeGen;
class SandboxUnittestHelper;
struct Instruction;

class Sandbox {
 public:
  enum SandboxStatus {
    STATUS_UNKNOWN,      // Status prior to calling supportsSeccompSandbox()
    STATUS_UNSUPPORTED,  // The kernel does not appear to support sandboxing
    STATUS_UNAVAILABLE,  // Currently unavailable but might work again later
    STATUS_AVAILABLE,    // Sandboxing is available but not currently active
    STATUS_ENABLED       // The sandbox is now active
  };

  // TrapFnc is a pointer to a function that handles Seccomp traps in
  // user-space. The seccomp policy can request that a trap handler gets
  // installed; it does so by returning a suitable ErrorCode() from the
  // syscallEvaluator. See the ErrorCode() constructor for how to pass in
  // the function pointer.
  // Please note that TrapFnc is executed from signal context and must be
  // async-signal safe:
  // http://pubs.opengroup.org/onlinepubs/009695399/functions/xsh_chap02_04.html
  // Also note that it follows the calling convention of native system calls.
  // In other words, it reports an error by returning an exit code in the
  // range -1..-4096. It should not set errno when reporting errors; on the
  // other hand, accidentally modifying errno is harmless and the changes will
  // be undone afterwards.
  typedef intptr_t (*TrapFnc)(const struct arch_seccomp_data& args, void *aux);

  enum Operation {
    OP_NOP, OP_EQUAL, OP_NOTEQUAL, OP_LESS,
    OP_LESS_EQUAL, OP_GREATER, OP_GREATER_EQUAL,
    OP_HAS_BITS, OP_DOES_NOT_HAVE_BITS
  };

  struct Constraint {
    bool      is32bit;
    Operation op;
    uint32_t  value;
    ErrorCode passed;
    ErrorCode failed;
  };

  // When calling setSandboxPolicy(), the caller can provide an arbitrary
  // pointer. This pointer will then be forwarded to the sandbox policy
  // each time a call is made through an EvaluateSyscall function pointer.
  // One common use case would be to pass the "aux" pointer as an argument
  // to Trap() functions.
  typedef ErrorCode (*EvaluateSyscall)(int sysnum, void *aux);
  typedef std::vector<std::pair<EvaluateSyscall, void *> >Evaluators;

  // Checks whether a particular system call number is valid on the current
  // architecture. E.g. on ARM there's a non-contiguous range of private
  // system calls.
  static bool isValidSyscallNumber(int sysnum);

  // There are a lot of reasons why the Seccomp sandbox might not be available.
  // This could be because the kernel does not support Seccomp mode, or it
  // could be because another sandbox is already active.
  // "proc_fd" should be a file descriptor for "/proc", or -1 if not
  // provided by the caller.
  static SandboxStatus supportsSeccompSandbox(int proc_fd);

  // The sandbox needs to be able to access files in "/proc/self". If this
  // directory is not accessible when "startSandbox()" gets called, the caller
  // can provide an already opened file descriptor by calling "setProcFd()".
  // The sandbox becomes the new owner of this file descriptor and will
  // eventually close it when "startSandbox()" executes.
  static void setProcFd(int proc_fd);

  // The system call evaluator function is called with the system
  // call number. It can decide to allow the system call unconditionally
  // by returning ERR_ALLOWED; it can deny the system call unconditionally by
  // returning an appropriate "errno" value; or it can request inspection
  // of system call argument(s) by returning a suitable ErrorCode.
  // The "aux" parameter can be used to pass optional data to the system call
  // evaluator. There are different possible uses for this data, but one of the
  // use cases would be for the policy to then forward this pointer to a Trap()
  // handler. In this case, of course, the data that is pointed to must remain
  // valid for the entire time that Trap() handlers can be called; typically,
  // this would be the lifetime of the program.
  static void setSandboxPolicy(EvaluateSyscall syscallEvaluator, void *aux);

  // We can use ErrorCode to request calling of a trap handler. This method
  // performs the required wrapping of the callback function into an
  // ErrorCode object.
  // The "aux" field can carry a pointer to arbitrary data. See EvaluateSyscall
  // for a description of how to pass data from setSandboxPolicy() to a Trap()
  // handler.
  static ErrorCode Trap(ErrorCode::TrapFnc fnc, const void *aux);

  // Calls a user-space trap handler and disables all sandboxing for system
  // calls made from this trap handler.
  // NOTE: This feature, by definition, disables all security features of
  //   the sandbox. It should never be used in production, but it can be
  //   very useful to diagnose code that is incompatible with the sandbox.
  //   If even a single system call returns "UnsafeTrap", the security of
  //   entire sandbox should be considered compromised.
  static ErrorCode UnsafeTrap(ErrorCode::TrapFnc fnc, const void *aux);

  // From within an UnsafeTrap() it is often useful to be able to execute
  // the system call that triggered the trap. The ForwardSyscall() method
  // makes this easy. It is more efficient than calling glibc's syscall()
  // function, as it avoid the extra round-trip to the signal handler. And
  // it automatically does the correct thing to report kernel-style error
  // conditions, rather than setting errno. See the comments for TrapFnc for
  // details. In other words, the return value from ForwardSyscall() is
  // directly suitable as a return value for a trap handler.
  static intptr_t ForwardSyscall(const struct arch_seccomp_data& args);

  // Kill the program and print an error message.
  static ErrorCode Kill(const char *msg);

  // This is the main public entry point. It finds all system calls that
  // need rewriting, sets up the resources needed by the sandbox, and
  // enters Seccomp mode.
  static void startSandbox() { startSandboxInternal(false); }

 private:
  friend class ErrorCode;
  friend class CodeGen;
  friend class SandboxUnittestHelper;
  friend class Util;
  friend class Verifier;

  typedef std::vector<struct sock_filter> Program;

  struct Range {
    Range(uint32_t f, uint32_t t, const ErrorCode& e)
        : from(f),
          to(t),
          err(e) {
    }
    uint32_t  from, to;
    ErrorCode err;
  };
  struct TrapKey {
    TrapKey(TrapFnc f, const void *a, bool s)
        : fnc(f),
          aux(a),
          safe(s) {
    }
    TrapFnc    fnc;
    const void *aux;
    bool       safe;
    bool operator<(const TrapKey&) const;
  };
  typedef std::vector<Range> Ranges;
  typedef std::map<uint32_t, ErrorCode> ErrMap;
  typedef std::vector<ErrorCode> Traps;
  typedef std::map<TrapKey, uint16_t> TrapIds;

  // Get a file descriptor pointing to "/proc", if currently available.
  static int proc_fd() { return proc_fd_; }

  static ErrorCode probeEvaluator(int sysnum, void *) __attribute__((const));
  static void      probeProcess(void);
  static ErrorCode allowAllEvaluator(int sysnum, void *aux);
  static void      tryVsyscallProcess(void);
  static bool      kernelSupportSeccompBPF(int proc_fd);
  static bool      RunFunctionInPolicy(void (*function)(),
                                       EvaluateSyscall syscallEvaluator,
                                       void *aux,
                                       int proc_fd);
  static void      startSandboxInternal(bool quiet);
  static bool      isSingleThreaded(int proc_fd);
  static bool      isDenied(const ErrorCode& code);
  static bool      disableFilesystem();
  static void      policySanityChecks(EvaluateSyscall syscallEvaluator,
                                      void *aux);

  // Function that can be passed as a callback function to CodeGen::Traverse().
  // Checks whether the "insn" returns an UnsafeTrap() ErrorCode. If so, it
  // sets the "bool" variable pointed to by "aux".
  static void      CheckForUnsafeErrorCodes(Instruction *insn, void *aux);

  // Function that can be passed as a callback function to CodeGen::Traverse().
  // Checks whether the "insn" returns an errno value from a BPF filter. If so,
  // it rewrites the instruction to instead call a Trap() handler that does
  // the same thing. "aux" is ignored.
  static void      RedirectToUserspace(Instruction *insn, void *aux);

  // Stackable wrapper around an Evaluators handler. Changes ErrorCodes
  // returned by a system call evaluator to match the changes made by
  // RedirectToUserspace(). "aux" should be pointer to wrapped system call
  // evaluator.
  static ErrorCode RedirectToUserspaceEvalWrapper(int sysnum, void *aux);

  static void      installFilter(bool quiet);
  static void      findRanges(Ranges *ranges);
  static Instruction *assembleJumpTable(CodeGen *gen,
                                        Ranges::const_iterator start,
                                        Ranges::const_iterator stop);
  static void      sigSys(int nr, siginfo_t *info, void *void_context);
  static ErrorCode MakeTrap(ErrorCode::TrapFnc fn, const void *aux, bool safe);

  // A Trap() handler that returns an "errno" value. The value is encoded
  // in the "aux" parameter.
  static intptr_t  ReturnErrno(const struct arch_seccomp_data&, void *aux);

  static intptr_t  bpfFailure(const struct arch_seccomp_data& data, void *aux);
  static int       getTrapId(TrapFnc fnc, const void *aux);

  static SandboxStatus status_;
  static int           proc_fd_;
  static Evaluators    evaluators_;
  static Traps         *traps_;
  static TrapIds       trapIds_;
  static ErrorCode     *trapArray_;
  static size_t        trapArraySize_;
  static bool          has_unsafe_traps_;
  DISALLOW_IMPLICIT_CONSTRUCTORS(Sandbox);
};

}  // namespace

#endif  // SANDBOX_LINUX_SECCOMP_BPF_SANDBOX_BPF_H__
