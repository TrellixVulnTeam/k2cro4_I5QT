// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// For linux_syscall_support.h. This makes it safe to call embedded system
// calls when in seccomp mode.
#define SYS_SYSCALL_ENTRYPOINT "playground$syscallEntryPoint"

#include "chrome/app/breakpad_linux.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <string>

#include "base/command_line.h"
#include "base/file_path.h"
#include "base/linux_util.h"
#include "base/path_service.h"
#include "base/posix/eintr_wrapper.h"
#include "base/posix/global_descriptors.h"
#include "base/process_util.h"
#include "base/string_util.h"
#include "breakpad/src/client/linux/handler/exception_handler.h"
#include "breakpad/src/client/linux/minidump_writer/directory_reader.h"
#include "breakpad/src/common/linux/linux_libc_support.h"
#include "breakpad/src/common/memory.h"
#include "chrome/browser/crash_upload_list.h"
#include "chrome/common/child_process_logging.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/chrome_version_info_posix.h"
#include "chrome/common/env_vars.h"
#include "chrome/common/logging_chrome.h"
#include "content/public/common/content_descriptors.h"

#if defined(OS_ANDROID)
#include <android/log.h>
#include <sys/stat.h>

#include "base/android/build_info.h"
#include "base/android/path_utils.h"
#include "third_party/lss/linux_syscall_support.h"
#else
#include "sandbox/linux/seccomp-legacy/linux_syscall_support.h"
#endif

#if defined(ADDRESS_SANITIZER)
#include <ucontext.h>  // for getcontext().
#endif

#if defined(OS_ANDROID)
#define STAT_STRUCT struct stat
#define FSTAT_FUNC fstat
#else
#define STAT_STRUCT struct kernel_stat
#define FSTAT_FUNC sys_fstat
#endif

// Some versions of gcc are prone to warn about unused return values. In cases
// where we either a) know the call cannot fail, or b) there is nothing we
// can do when a call fails, we mark the return code as ignored. This avoids
// spurious compiler warnings.
#define IGNORE_RET(x) do { if (x); } while (0)

using google_breakpad::ExceptionHandler;
using google_breakpad::MinidumpDescriptor;

namespace {

#if !defined(ADDRESS_SANITIZER)
const char kUploadURL[] = "https://clients2.google.com/cr/report";
#else
// AddressSanitizer should currently upload the crash reports to the staging
// crash server.
const char kUploadURL[] = "https://clients2.google.com/cr/staging_report";
#endif

bool g_is_crash_reporter_enabled = false;
uint64_t g_process_start_time = 0;
char* g_crash_log_path = NULL;
ExceptionHandler* g_breakpad = NULL;
#if defined(ADDRESS_SANITIZER)
const char* g_asan_report_str = NULL;
#endif
#if defined(OS_ANDROID)
char* g_process_type = NULL;
#endif

// Writes the value |v| as 16 hex characters to the memory pointed at by
// |output|.
void write_uint64_hex(char* output, uint64_t v) {
  static const char hextable[] = "0123456789abcdef";

  for (int i = 15; i >= 0; --i) {
    output[i] = hextable[v & 15];
    v >>= 4;
  }
}

// The following helper functions are for calculating uptime.

// Converts a struct timeval to milliseconds.
uint64_t timeval_to_ms(struct timeval *tv) {
  uint64_t ret = tv->tv_sec;  // Avoid overflow by explicitly using a uint64_t.
  ret *= 1000;
  ret += tv->tv_usec / 1000;
  return ret;
}

// Converts a struct timeval to milliseconds.
uint64_t kernel_timeval_to_ms(struct kernel_timeval *tv) {
  uint64_t ret = tv->tv_sec;  // Avoid overflow by explicitly using a uint64_t.
  ret *= 1000;
  ret += tv->tv_usec / 1000;
  return ret;
}

// String buffer size to use to convert a uint64_t to string.
size_t kUint64StringSize = 21;

void SetProcessStartTime() {
  // Set the base process start time value.
  struct timeval tv;
  if (!gettimeofday(&tv, NULL))
    g_process_start_time = timeval_to_ms(&tv);
  else
    g_process_start_time = 0;
}

// uint64_t version of my_int_len() from
// breakpad/src/common/linux/linux_libc_support.h. Return the length of the
// given, non-negative integer when expressed in base 10.
unsigned my_uint64_len(uint64_t i) {
  if (!i)
    return 1;

  unsigned len = 0;
  while (i) {
    len++;
    i /= 10;
  }

  return len;
}

// uint64_t version of my_uitos() from
// breakpad/src/common/linux/linux_libc_support.h. Convert a non-negative
// integer to a string (not null-terminated).
void my_uint64tos(char* output, uint64_t i, unsigned i_len) {
  for (unsigned index = i_len; index; --index, i /= 10)
    output[index - 1] = '0' + (i % 10);
}

#if defined(OS_ANDROID)
char* my_strncpy(char* dst, const char* src, size_t len) {
  int i = len;
  char* p = dst;
  if (!dst || !src)
    return dst;
  while (i != 0 && *src != '\0') {
    *p++ = *src++;
    i--;
  }
  while (i != 0) {
    *p++ = '\0';
    i--;
  }
  return dst;
}

char* my_strncat(char *dest, const char* src, size_t len) {
  char* ret = dest;
  while (*dest)
      dest++;
  while (len--)
    if (!(*dest++ = *src++))
      return ret;
  *dest = 0;
  return ret;
}
#endif

// Populates the passed in allocated strings and their sizes with the GUID,
// crash url and distro of the crashing process.
// The passed strings are expected to be at least kGuidSize, kMaxActiveURLSize
// and kDistroSize bytes long respectively.
void PopulateGUIDAndURLAndDistro(char* guid, size_t* guid_len_param,
                                 char* crash_url, size_t* crash_url_len_param,
                                 char* distro, size_t* distro_len_param) {
  size_t guid_len = std::min(my_strlen(child_process_logging::g_client_id),
                             kGuidSize);
  size_t crash_url_len =
      std::min(my_strlen(child_process_logging::g_active_url),
               kMaxActiveURLSize);
  size_t distro_len = std::min(my_strlen(base::g_linux_distro), kDistroSize);
  memcpy(guid, child_process_logging::g_client_id, guid_len);
  memcpy(crash_url, child_process_logging::g_active_url, crash_url_len);
  memcpy(distro, base::g_linux_distro, distro_len);
  if (guid_len_param)
    *guid_len_param = guid_len;
  if (crash_url_len_param)
    *crash_url_len_param = crash_url_len;
  if (distro_len_param)
    *distro_len_param = distro_len;
}

// MIME substrings.
const char g_rn[] = "\r\n";
const char g_form_data_msg[] = "Content-Disposition: form-data; name=\"";
const char g_quote_msg[] = "\"";
const char g_dashdash_msg[] = "--";
const char g_dump_msg[] = "upload_file_minidump\"; filename=\"dump\"";
#if defined(ADDRESS_SANITIZER)
const char g_log_msg[] = "upload_file_log\"; filename=\"log\"";
#endif
const char g_content_type_msg[] = "Content-Type: application/octet-stream";

// MimeWriter manages an iovec for writing MIMEs to a file.
class MimeWriter {
 public:
  static const int kIovCapacity = 30;
  static const size_t kMaxCrashChunkSize = 64;

  MimeWriter(int fd, const char* const mime_boundary);
  ~MimeWriter();

  // Append boundary.
  void AddBoundary();

  // Append end of file boundary.
  void AddEnd();

  // Append key/value pair with specified sizes.
  void AddPairData(const char* msg_type,
                   size_t msg_type_size,
                   const char* msg_data,
                   size_t msg_data_size);

  // Append key/value pair.
  void AddPairString(const char* msg_type,
                     const char* msg_data) {
    AddPairData(msg_type, my_strlen(msg_type), msg_data, my_strlen(msg_data));
  }

  // Append key/value pair, splitting value into chunks no larger than
  // |chunk_size|. |chunk_size| cannot be greater than |kMaxCrashChunkSize|.
  // The msg_type string will have a counter suffix to distinguish each chunk.
  void AddPairDataInChunks(const char* msg_type,
                           size_t msg_type_size,
                           const char* msg_data,
                           size_t msg_data_size,
                           size_t chunk_size,
                           bool strip_trailing_spaces);

  // Add binary file contents to be uploaded with the specified filename.
  void AddFileContents(const char* filename_msg,
                       uint8_t* file_data,
                       size_t file_size);

  // Flush any pending iovecs to the output file.
  void Flush() {
    IGNORE_RET(sys_writev(fd_, iov_, iov_index_));
    iov_index_ = 0;
  }

 private:
  void AddItem(const void* base, size_t size);
  // Minor performance trade-off for easier-to-maintain code.
  void AddString(const char* str) {
    AddItem(str, my_strlen(str));
  }
  void AddItemWithoutTrailingSpaces(const void* base, size_t size);

  struct kernel_iovec iov_[kIovCapacity];
  int iov_index_;

  // Output file descriptor.
  int fd_;

  const char* const mime_boundary_;

  DISALLOW_COPY_AND_ASSIGN(MimeWriter);
};

MimeWriter::MimeWriter(int fd, const char* const mime_boundary)
    : iov_index_(0),
      fd_(fd),
      mime_boundary_(mime_boundary) {
}

MimeWriter::~MimeWriter() {
}

void MimeWriter::AddBoundary() {
  AddString(mime_boundary_);
  AddString(g_rn);
}

void MimeWriter::AddEnd() {
  AddString(mime_boundary_);
  AddString(g_dashdash_msg);
  AddString(g_rn);
}

void MimeWriter::AddPairData(const char* msg_type,
                             size_t msg_type_size,
                             const char* msg_data,
                             size_t msg_data_size) {
  AddString(g_form_data_msg);
  AddItem(msg_type, msg_type_size);
  AddString(g_quote_msg);
  AddString(g_rn);
  AddString(g_rn);
  AddItem(msg_data, msg_data_size);
  AddString(g_rn);
}

void MimeWriter::AddPairDataInChunks(const char* msg_type,
                                     size_t msg_type_size,
                                     const char* msg_data,
                                     size_t msg_data_size,
                                     size_t chunk_size,
                                     bool strip_trailing_spaces) {
  if (chunk_size > kMaxCrashChunkSize)
    return;

  unsigned i = 0;
  size_t done = 0, msg_length = msg_data_size;

  while (msg_length) {
    char num[16];
    const unsigned num_len = my_uint_len(++i);
    my_uitos(num, i, num_len);

    size_t chunk_len = std::min(chunk_size, msg_length);

    AddString(g_form_data_msg);
    AddItem(msg_type, msg_type_size);
    AddItem(num, num_len);
    AddString(g_quote_msg);
    AddString(g_rn);
    AddString(g_rn);
    if (strip_trailing_spaces) {
      AddItemWithoutTrailingSpaces(msg_data + done, chunk_len);
    } else {
      AddItem(msg_data + done, chunk_len);
    }
    AddString(g_rn);
    AddBoundary();
    Flush();

    done += chunk_len;
    msg_length -= chunk_len;
  }
}

void MimeWriter::AddFileContents(const char* filename_msg, uint8_t* file_data,
                                 size_t file_size) {
  AddString(g_form_data_msg);
  AddString(filename_msg);
  AddString(g_rn);
  AddString(g_content_type_msg);
  AddString(g_rn);
  AddString(g_rn);
  AddItem(file_data, file_size);
  AddString(g_rn);
}

void MimeWriter::AddItem(const void* base, size_t size) {
  // Check if the iovec is full and needs to be flushed to output file.
  if (iov_index_ == kIovCapacity) {
    Flush();
  }
  iov_[iov_index_].iov_base = const_cast<void*>(base);
  iov_[iov_index_].iov_len = size;
  ++iov_index_;
}

void MimeWriter::AddItemWithoutTrailingSpaces(const void* base, size_t size) {
  while (size > 0) {
    const char* c = static_cast<const char*>(base) + size - 1;
    if (*c != ' ')
      break;
    size--;
  }
  AddItem(base, size);
}

void DumpProcess() {
  if (g_breakpad)
    g_breakpad->WriteMinidump();
}

const char kGoogleBreakpad[] = "google-breakpad";

size_t WriteLog(const char* buf, size_t nbytes) {
#if defined(OS_ANDROID)
  return __android_log_write(ANDROID_LOG_WARN, kGoogleBreakpad, buf);
#else
  return sys_write(2, buf, nbytes);
#endif
}

#if defined(OS_ANDROID)
// Android's native crash handler outputs a diagnostic tombstone to the device
// log. By returning false from the HandlerCallbacks, breakpad will reinstall
// the previous (i.e. native) signal handlers before returning from its own
// handler. A Chrome build fingerprint is written to the log, so that the
// specific build of Chrome and the location of the archived Chrome symbols can
// be determined directly from it.
bool FinalizeCrashDoneAndroid() {
  base::android::BuildInfo* android_build_info =
      base::android::BuildInfo::GetInstance();

  __android_log_write(ANDROID_LOG_WARN, kGoogleBreakpad,
                      "### ### ### ### ### ### ### ### ### ### ### ### ###");
  __android_log_write(ANDROID_LOG_WARN, kGoogleBreakpad,
                      "Chrome build fingerprint:");
  __android_log_write(ANDROID_LOG_WARN, kGoogleBreakpad,
                      android_build_info->package_version_name());
  __android_log_write(ANDROID_LOG_WARN, kGoogleBreakpad,
                      android_build_info->package_version_code());
  __android_log_write(ANDROID_LOG_WARN, kGoogleBreakpad,
                      CHROME_SYMBOLS_ID);
  __android_log_write(ANDROID_LOG_WARN, kGoogleBreakpad,
                      "### ### ### ### ### ### ### ### ### ### ### ### ###");
  return false;
}
#endif

bool CrashDone(const MinidumpDescriptor& minidump,
               const bool upload,
               const bool succeeded) {
  // WARNING: this code runs in a compromised context. It may not call into
  // libc nor allocate memory normally.
  if (!succeeded) {
    const char msg[] = "Failed to generate minidump.";
    WriteLog(msg, sizeof(msg) - 1);
    return false;
  }

  DCHECK(!minidump.IsFD());

  BreakpadInfo info = {0};
  info.filename = minidump.path();
  info.fd = minidump.fd();
#if defined(ADDRESS_SANITIZER)
  google_breakpad::PageAllocator allocator;
  const size_t log_path_len = my_strlen(minidump.path());
  char* log_path = reinterpret_cast<char*>(allocator.Alloc(log_path_len + 1));
  my_memcpy(log_path, minidump.path(), log_path_len);
  my_memcpy(log_path + log_path_len - 4, ".log", 4);
  log_path[log_path_len] = '\0';
  info.log_filename = log_path;
#endif
  info.process_type = "browser";
  info.process_type_length = 7;
  info.crash_url = NULL;
  info.crash_url_length = 0;
  info.guid = child_process_logging::g_client_id;
  info.guid_length = my_strlen(child_process_logging::g_client_id);
  info.distro = base::g_linux_distro;
  info.distro_length = my_strlen(base::g_linux_distro);
  info.upload = upload;
  info.process_start_time = g_process_start_time;
  info.oom_size = base::g_oom_size;
  info.pid = 0;
  HandleCrashDump(info);
#if defined(OS_ANDROID)
  return FinalizeCrashDoneAndroid();
#else
  return true;
#endif
}

// Wrapper function, do not add more code here.
bool CrashDoneNoUpload(const MinidumpDescriptor& minidump,
                       void* context,
                       bool succeeded) {
  return CrashDone(minidump, false, succeeded);
}

#if !defined(OS_ANDROID)
// Wrapper function, do not add more code here.
bool CrashDoneUpload(const MinidumpDescriptor& minidump,
                     void* context,
                     bool succeeded) {
  return CrashDone(minidump, true, succeeded);
}
#endif

#if defined(ADDRESS_SANITIZER)
extern "C"
void __asan_set_error_report_callback(void (*cb)(const char*));

extern "C"
void AsanLinuxBreakpadCallback(const char* report) {
  g_asan_report_str = report;
  // Send minidump here.
  g_breakpad->SimulateSignalDelivery(SIGKILL);
}
#endif

void EnableCrashDumping(bool unattended) {
  g_is_crash_reporter_enabled = true;

  FilePath tmp_path("/tmp");
  PathService::Get(base::DIR_TEMP, &tmp_path);

  FilePath dumps_path(tmp_path);
  if (PathService::Get(chrome::DIR_CRASH_DUMPS, &dumps_path)) {
    FilePath logfile =
        dumps_path.AppendASCII(CrashUploadList::kReporterLogFilename);
    std::string logfile_str = logfile.value();
    const size_t crash_log_path_len = logfile_str.size() + 1;
    g_crash_log_path = new char[crash_log_path_len];
    strncpy(g_crash_log_path, logfile_str.c_str(), crash_log_path_len);
  }
  DCHECK(!g_breakpad);
  MinidumpDescriptor minidump_descriptor(dumps_path.value());
  minidump_descriptor.set_size_limit(kMaxMinidumpFileSize);
#if defined(OS_ANDROID)
  unattended = true;  // Android never uploads directly.
#endif
  if (unattended) {
    g_breakpad = new ExceptionHandler(
        minidump_descriptor,
        NULL,
        CrashDoneNoUpload,
        NULL,
        true,  // Install handlers.
        -1);   // Server file descriptor. -1 for in-process.
    return;
  }

#if !defined(OS_ANDROID)
  // Attended mode
  g_breakpad = new ExceptionHandler(
      minidump_descriptor,
      NULL,
      CrashDoneUpload,
      NULL,
      true,  // Install handlers.
      -1);   // Server file descriptor. -1 for in-process.
#endif
}

#if defined(OS_ANDROID)
bool CrashDoneInProcessNoUpload(
    const google_breakpad::MinidumpDescriptor& descriptor,
    void* context,
    const bool succeeded) {
  // WARNING: this code runs in a compromised context. It may not call into
  // libc nor allocate memory normally.
  if (!succeeded) {
    static const char msg[] = "Crash dump generation failed.\n";
    WriteLog(msg, sizeof(msg) - 1);
    return false;
  }

  // Start constructing the message to send to the browser.
  char guid[kGuidSize + 1] = {0};
  char crash_url[kMaxActiveURLSize + 1] = {0};
  char distro[kDistroSize + 1] = {0};
  size_t guid_length = 0;
  size_t crash_url_length = 0;
  size_t distro_length = 0;
  PopulateGUIDAndURLAndDistro(guid, &guid_length, crash_url, &crash_url_length,
                              distro, &distro_length);
  BreakpadInfo info = {0};
  info.filename = NULL;
  info.fd = descriptor.fd();
  info.process_type = g_process_type;
  info.process_type_length = my_strlen(g_process_type);
  info.crash_url = crash_url;
  info.crash_url_length = crash_url_length;
  info.guid = guid;
  info.guid_length = guid_length;
  info.distro = distro;
  info.distro_length = distro_length;
  info.upload = false;
  info.process_start_time = g_process_start_time;
  HandleCrashDump(info);
  return FinalizeCrashDoneAndroid();
}

void EnableNonBrowserCrashDumping(int minidump_fd) {
  // This will guarantee that the BuildInfo has been initialized and subsequent
  // calls will not require memory allocation.
  base::android::BuildInfo::GetInstance();
  child_process_logging::SetClientId("Android");

  // On Android, the current sandboxing uses process isolation, in which the
  // child process runs with a different UID. That breaks the normal crash
  // reporting where the browser process generates the minidump by inspecting
  // the child process. This is because the browser process now does not have
  // the permission to access the states of the child process (as it has a
  // different UID).
  // TODO(jcivelli): http://b/issue?id=6776356 we should use a watchdog
  // process forked from the renderer process that generates the minidump.
  if (minidump_fd == -1) {
    LOG(ERROR) << "Minidump file descriptor not found, crash reporting will "
        " not work.";
    return;
  }
  SetProcessStartTime();

  g_is_crash_reporter_enabled = true;
  // Save the process type (it is leaked).
  const CommandLine& parsed_command_line = *CommandLine::ForCurrentProcess();
  const std::string process_type =
      parsed_command_line.GetSwitchValueASCII(switches::kProcessType);
  const size_t process_type_len = process_type.size() + 1;
  g_process_type = new char[process_type_len];
  strncpy(g_process_type, process_type.c_str(), process_type_len);
  new google_breakpad::ExceptionHandler(MinidumpDescriptor(minidump_fd),
      NULL, CrashDoneInProcessNoUpload, NULL, true, -1);
}
#else
// Non-Browser = Extension, Gpu, Plugins, Ppapi and Renderer
bool NonBrowserCrashHandler(const void* crash_context,
                            size_t crash_context_size,
                            void* context) {
  const int fd = reinterpret_cast<intptr_t>(context);
  int fds[2] = { -1, -1 };
  if (sys_socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
    static const char msg[] = "Failed to create socket for crash dumping.\n";
    WriteLog(msg, sizeof(msg) - 1);
    return false;
  }

  // Start constructing the message to send to the browser.
  char guid[kGuidSize + 1] = {0};
  char crash_url[kMaxActiveURLSize + 1] = {0};
  char distro[kDistroSize + 1] = {0};
  PopulateGUIDAndURLAndDistro(guid, NULL, crash_url, NULL, distro, NULL);

  char b;  // Dummy variable for sys_read below.
  const char* b_addr = &b;  // Get the address of |b| so we can create the
                            // expected /proc/[pid]/syscall content in the
                            // browser to convert namespace tids.

  // The length of the control message:
  static const unsigned kControlMsgSize = sizeof(fds);
  static const unsigned kControlMsgSpaceSize = CMSG_SPACE(kControlMsgSize);
  static const unsigned kControlMsgLenSize = CMSG_LEN(kControlMsgSize);

#if !defined(ADDRESS_SANITIZER)
  const size_t kIovSize = 8;
#else
  // Additional field to pass the AddressSanitizer log to the crash handler.
  const size_t kIovSize = 9;
#endif
  struct kernel_msghdr msg;
  my_memset(&msg, 0, sizeof(struct kernel_msghdr));
  struct kernel_iovec iov[kIovSize];
  iov[0].iov_base = const_cast<void*>(crash_context);
  iov[0].iov_len = crash_context_size;
  iov[1].iov_base = guid;
  iov[1].iov_len = kGuidSize + 1;
  iov[2].iov_base = crash_url;
  iov[2].iov_len = kMaxActiveURLSize + 1;
  iov[3].iov_base = distro;
  iov[3].iov_len = kDistroSize + 1;
  iov[4].iov_base = &b_addr;
  iov[4].iov_len = sizeof(b_addr);
  iov[5].iov_base = &fds[0];
  iov[5].iov_len = sizeof(fds[0]);
  iov[6].iov_base = &g_process_start_time;
  iov[6].iov_len = sizeof(g_process_start_time);
  iov[7].iov_base = &base::g_oom_size;
  iov[7].iov_len = sizeof(base::g_oom_size);
#if defined(ADDRESS_SANITIZER)
  iov[8].iov_base = const_cast<char*>(g_asan_report_str);
  iov[8].iov_len = kMaxAsanReportSize + 1;
#endif

  msg.msg_iov = iov;
  msg.msg_iovlen = kIovSize;
  char cmsg[kControlMsgSpaceSize];
  my_memset(cmsg, 0, kControlMsgSpaceSize);
  msg.msg_control = cmsg;
  msg.msg_controllen = sizeof(cmsg);

  struct cmsghdr *hdr = CMSG_FIRSTHDR(&msg);
  hdr->cmsg_level = SOL_SOCKET;
  hdr->cmsg_type = SCM_RIGHTS;
  hdr->cmsg_len = kControlMsgLenSize;
  ((int*) CMSG_DATA(hdr))[0] = fds[0];
  ((int*) CMSG_DATA(hdr))[1] = fds[1];

  if (HANDLE_EINTR(sys_sendmsg(fd, &msg, 0)) < 0) {
    static const char errmsg[] = "Failed to tell parent about crash.\n";
    WriteLog(errmsg, sizeof(errmsg) - 1);
    IGNORE_RET(sys_close(fds[1]));
    return false;
  }
  IGNORE_RET(sys_close(fds[1]));

  if (HANDLE_EINTR(sys_read(fds[0], &b, 1)) != 1) {
    static const char errmsg[] = "Parent failed to complete crash dump.\n";
    WriteLog(errmsg, sizeof(errmsg) - 1);
  }

  return true;
}

void EnableNonBrowserCrashDumping() {
  const int fd = base::GlobalDescriptors::GetInstance()->Get(kCrashDumpSignal);
  g_is_crash_reporter_enabled = true;
  // We deliberately leak this object.
  DCHECK(!g_breakpad);

  g_breakpad = new ExceptionHandler(
      MinidumpDescriptor("/tmp"),  // Unused but needed or Breakpad will assert.
      NULL,
      NULL,
      reinterpret_cast<void*>(fd),  // Param passed to the crash handler.
      true,
      -1);
  g_breakpad->set_crash_handler(NonBrowserCrashHandler);
}
#endif  // defined(OS_ANDROID)

}  // namespace

void LoadDataFromFD(google_breakpad::PageAllocator& allocator,
                    int fd, bool close_fd, uint8_t** file_data, size_t* size) {
  STAT_STRUCT st;
  if (FSTAT_FUNC(fd, &st) != 0) {
    static const char msg[] = "Cannot upload crash dump: stat failed\n";
    WriteLog(msg, sizeof(msg) - 1);
    if (close_fd)
      IGNORE_RET(sys_close(fd));
    return;
  }

  *file_data = reinterpret_cast<uint8_t*>(allocator.Alloc(st.st_size));
  if (!(*file_data)) {
    static const char msg[] = "Cannot upload crash dump: cannot alloc\n";
    WriteLog(msg, sizeof(msg) - 1);
    if (close_fd)
      IGNORE_RET(sys_close(fd));
    return;
  }
  my_memset(*file_data, 0xf, st.st_size);

  *size = st.st_size;
  int byte_read = sys_read(fd, *file_data, *size);
  if (byte_read == -1) {
    static const char msg[] = "Cannot upload crash dump: read failed\n";
    WriteLog(msg, sizeof(msg) - 1);
    if (close_fd)
      IGNORE_RET(sys_close(fd));
    return;
  }

  if (close_fd)
    IGNORE_RET(sys_close(fd));
}

void LoadDataFromFile(google_breakpad::PageAllocator& allocator,
                      const char* filename,
                      int* fd, uint8_t** file_data, size_t* size) {
  // WARNING: this code runs in a compromised context. It may not call into
  // libc nor allocate memory normally.
  *fd = sys_open(filename, O_RDONLY, 0);
  *size = 0;

  if (*fd < 0) {
    static const char msg[] = "Cannot upload crash dump: failed to open\n";
    WriteLog(msg, sizeof(msg) - 1);
    return;
  }

  LoadDataFromFD(allocator, *fd, true, file_data, size);
}

void HandleCrashDump(const BreakpadInfo& info) {
  int dumpfd;
  bool keep_fd = false;
  size_t dump_size;
  uint8_t* dump_data;
  google_breakpad::PageAllocator allocator;

  if (info.fd != -1) {
    // Dump is provided with an open FD.
    keep_fd = true;
    dumpfd = info.fd;

    // The FD is pointing to the end of the file.
    // Rewind, we'll read the data next.
    if (lseek(dumpfd, 0, SEEK_SET) == -1) {
      static const char msg[] = "Cannot upload crash dump: failed to "
          "reposition minidump FD\n";
      WriteLog(msg, sizeof(msg) - 1);
      IGNORE_RET(sys_close(dumpfd));
      return;
    }
    LoadDataFromFD(allocator, info.fd, false, &dump_data, &dump_size);
  } else {
    // Dump is provided with a path.
    keep_fd = false;
    LoadDataFromFile(allocator, info.filename, &dumpfd, &dump_data, &dump_size);
  }

  // TODO(jcivelli): make log work when using FDs.
#if defined(ADDRESS_SANITIZER)
  int logfd;
  size_t log_size;
  uint8_t* log_data;
  // Load the AddressSanitizer log into log_data.
  LoadDataFromFile(allocator, info.log_filename, &logfd, &log_data, &log_size);
#endif

  // We need to build a MIME block for uploading to the server. Since we are
  // going to fork and run wget, it needs to be written to a temp file.
  const int ufd = sys_open("/dev/urandom", O_RDONLY, 0);
  if (ufd < 0) {
    static const char msg[] = "Cannot upload crash dump because /dev/urandom"
                              " is missing\n";
    WriteLog(msg, sizeof(msg) - 1);
    return;
  }

  static const char temp_file_template[] =
      "/tmp/chromium-upload-XXXXXXXXXXXXXXXX";
  char temp_file[sizeof(temp_file_template)];
  int temp_file_fd = -1;
  if (keep_fd) {
    temp_file_fd = dumpfd;
    // Rewind the destination, we are going to overwrite it.
    if (lseek(dumpfd, 0, SEEK_SET) == -1) {
      static const char msg[] = "Cannot upload crash dump: failed to "
          "reposition minidump FD (2)\n";
      WriteLog(msg, sizeof(msg) - 1);
      IGNORE_RET(sys_close(dumpfd));
      return;
    }
  } else {
    if (info.upload) {
      memcpy(temp_file, temp_file_template, sizeof(temp_file_template));

      for (unsigned i = 0; i < 10; ++i) {
        uint64_t t;
        sys_read(ufd, &t, sizeof(t));
        write_uint64_hex(temp_file + sizeof(temp_file) - (16 + 1), t);

        temp_file_fd = sys_open(temp_file, O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (temp_file_fd >= 0)
          break;
      }

      if (temp_file_fd < 0) {
        static const char msg[] = "Failed to create temporary file in /tmp: "
            "cannot upload crash dump\n";
        WriteLog(msg, sizeof(msg) - 1);
        IGNORE_RET(sys_close(ufd));
        return;
      }
    } else {
      temp_file_fd = sys_open(info.filename, O_WRONLY, 0600);
      if (temp_file_fd < 0) {
        static const char msg[] = "Failed to save crash dump: failed to open\n";
        WriteLog(msg, sizeof(msg) - 1);
        IGNORE_RET(sys_close(ufd));
        return;
      }
    }
  }

  // The MIME boundary is 28 hyphens, followed by a 64-bit nonce and a NUL.
  char mime_boundary[28 + 16 + 1];
  my_memset(mime_boundary, '-', 28);
  uint64_t boundary_rand;
  sys_read(ufd, &boundary_rand, sizeof(boundary_rand));
  write_uint64_hex(mime_boundary + 28, boundary_rand);
  mime_boundary[28 + 16] = 0;
  IGNORE_RET(sys_close(ufd));

  // The MIME block looks like this:
  //   BOUNDARY \r\n
  //   Content-Disposition: form-data; name="prod" \r\n \r\n
  //   Chrome_Linux \r\n
  //   BOUNDARY \r\n
  //   Content-Disposition: form-data; name="ver" \r\n \r\n
  //   1.2.3.4 \r\n
  //   BOUNDARY \r\n
  //   Content-Disposition: form-data; name="guid" \r\n \r\n
  //   1.2.3.4 \r\n
  //   BOUNDARY \r\n
  //
  //   zero or one:
  //   Content-Disposition: form-data; name="ptime" \r\n \r\n
  //   abcdef \r\n
  //   BOUNDARY \r\n
  //
  //   zero or one:
  //   Content-Disposition: form-data; name="ptype" \r\n \r\n
  //   abcdef \r\n
  //   BOUNDARY \r\n
  //
  //   zero or more gpu entries:
  //   Content-Disposition: form-data; name="gpu-xxxxx" \r\n \r\n
  //   <gpu-xxxxx> \r\n
  //   BOUNDARY \r\n
  //
  //   zero or one:
  //   Content-Disposition: form-data; name="lsb-release" \r\n \r\n
  //   abcdef \r\n
  //   BOUNDARY \r\n
  //
  //   zero or more:
  //   Content-Disposition: form-data; name="url-chunk-1" \r\n \r\n
  //   abcdef \r\n
  //   BOUNDARY \r\n
  //
  //   zero or one:
  //   Content-Disposition: form-data; name="channel" \r\n \r\n
  //   beta \r\n
  //   BOUNDARY \r\n
  //
  //   zero or one:
  //   Content-Disposition: form-data; name="num-views" \r\n \r\n
  //   3 \r\n
  //   BOUNDARY \r\n
  //
  //   zero or one:
  //   Content-Disposition: form-data; name="num-extensions" \r\n \r\n
  //   5 \r\n
  //   BOUNDARY \r\n
  //
  //   zero to 10:
  //   Content-Disposition: form-data; name="extension-1" \r\n \r\n
  //   abcdefghijklmnopqrstuvwxyzabcdef \r\n
  //   BOUNDARY \r\n
  //
  //   zero to 4:
  //   Content-Disposition: form-data; name="prn-info-1" \r\n \r\n
  //   abcdefghijklmnopqrstuvwxyzabcdef \r\n
  //   BOUNDARY \r\n
  //
  //   zero or one:
  //   Content-Disposition: form-data; name="num-switches" \r\n \r\n
  //   5 \r\n
  //   BOUNDARY \r\n
  //
  //   zero to 15:
  //   Content-Disposition: form-data; name="switch-1" \r\n \r\n
  //   --foo \r\n
  //   BOUNDARY \r\n
  //
  //   zero or one:
  //   Content-Disposition: form-data; name="oom-size" \r\n \r\n
  //   1234567890 \r\n
  //   BOUNDARY \r\n
  //
  //   Content-Disposition: form-data; name="dump"; filename="dump" \r\n
  //   Content-Type: application/octet-stream \r\n \r\n
  //   <dump contents>
  //   \r\n BOUNDARY -- \r\n

  MimeWriter writer(temp_file_fd, mime_boundary);
  {
#if defined(OS_ANDROID)
    static const char chrome_product_msg[] = "Chrome_Android";
#elif defined(OS_CHROMEOS)
    static const char chrome_product_msg[] = "Chrome_ChromeOS";
#else  // OS_LINUX
    static const char chrome_product_msg[] = "Chrome_Linux";
#endif

#if defined (OS_ANDROID)
    base::android::BuildInfo* android_build_info =
        base::android::BuildInfo::GetInstance();
    static const char* version_msg =
        android_build_info->package_version_code();
#else
    static const char version_msg[] = PRODUCT_VERSION;
#endif

    writer.AddBoundary();
    writer.AddPairString("prod", chrome_product_msg);
    writer.AddBoundary();
    writer.AddPairString("ver", version_msg);
    writer.AddBoundary();
    writer.AddPairString("guid", info.guid);
    writer.AddBoundary();
    if (info.pid > 0) {
      char pid_value_buf[kUint64StringSize];
      uint64_t pid_value_len = my_uint64_len(info.pid);
      my_uint64tos(pid_value_buf, info.pid, pid_value_len);
      static const char pid_key_name[] = "pid";
      writer.AddPairData(pid_key_name, sizeof(pid_key_name) - 1,
                         pid_value_buf, pid_value_len);
      writer.AddBoundary();
    }
#if defined(OS_ANDROID)
    // Addtional MIME blocks are added for logging on Android devices.
    static const char android_build_id[] = "android_build_id";
    static const char android_build_fp[] = "android_build_fp";
    static const char device[] = "device";
    static const char model[] = "model";
    static const char brand[] = "brand";
    static const char exception_info[] = "exception_info";

    writer.AddPairString(
        android_build_id, android_build_info->android_build_id());
    writer.AddBoundary();
    writer.AddPairString(
        android_build_fp, android_build_info->android_build_fp());
    writer.AddBoundary();
    writer.AddPairString(device, android_build_info->device());
    writer.AddBoundary();
    writer.AddPairString(model, android_build_info->model());
    writer.AddBoundary();
    writer.AddPairString(brand, android_build_info->brand());
    writer.AddBoundary();
    if (android_build_info->java_exception_info() != NULL) {
      writer.AddPairString(exception_info,
                           android_build_info->java_exception_info());
      writer.AddBoundary();
    }
#endif
    writer.Flush();
  }

  if (info.process_start_time > 0) {
    struct kernel_timeval tv;
    if (!sys_gettimeofday(&tv, NULL)) {
      uint64_t time = kernel_timeval_to_ms(&tv);
      if (time > info.process_start_time) {
        time -= info.process_start_time;
        char time_str[kUint64StringSize];
        const unsigned time_len = my_uint64_len(time);
        my_uint64tos(time_str, time, time_len);

        static const char process_time_msg[] = "ptime";
        writer.AddPairData(process_time_msg, sizeof(process_time_msg) - 1,
                           time_str, time_len);
        writer.AddBoundary();
        writer.Flush();
      }
    }
  }

  if (info.process_type_length) {
    writer.AddPairString("ptype", info.process_type);
    writer.AddBoundary();
    writer.Flush();
  }

  // If GPU info is known, send it.
  if (*child_process_logging::g_gpu_vendor_id) {
    static const char vendor_msg[] = "gpu-venid";
    static const char device_msg[] = "gpu-devid";
    static const char driver_msg[] = "gpu-driver";
    static const char psver_msg[] = "gpu-psver";
    static const char vsver_msg[] = "gpu-vsver";

    writer.AddPairString(vendor_msg, child_process_logging::g_gpu_vendor_id);
    writer.AddBoundary();
    writer.AddPairString(device_msg, child_process_logging::g_gpu_device_id);
    writer.AddBoundary();
    writer.AddPairString(driver_msg, child_process_logging::g_gpu_driver_ver);
    writer.AddBoundary();
    writer.AddPairString(psver_msg, child_process_logging::g_gpu_ps_ver);
    writer.AddBoundary();
    writer.AddPairString(vsver_msg, child_process_logging::g_gpu_vs_ver);
    writer.AddBoundary();
    writer.Flush();
  }

  if (info.distro_length) {
    static const char distro_msg[] = "lsb-release";
    writer.AddPairString(distro_msg, info.distro);
    writer.AddBoundary();
    writer.Flush();
  }

  // For renderers and plugins.
  if (info.crash_url_length) {
    static const char url_chunk_msg[] = "url-chunk-";
    static const unsigned kMaxUrlLength = 8 * MimeWriter::kMaxCrashChunkSize;
    writer.AddPairDataInChunks(url_chunk_msg, sizeof(url_chunk_msg) - 1,
        info.crash_url, std::min(info.crash_url_length, kMaxUrlLength),
        MimeWriter::kMaxCrashChunkSize, false /* Don't strip whitespaces. */);
  }

  if (*child_process_logging::g_channel) {
    writer.AddPairString("channel", child_process_logging::g_channel);
    writer.AddBoundary();
    writer.Flush();
  }

  if (*child_process_logging::g_num_views) {
    writer.AddPairString("num-views", child_process_logging::g_num_views);
    writer.AddBoundary();
    writer.Flush();
  }

  if (*child_process_logging::g_num_extensions) {
    writer.AddPairString("num-extensions",
                         child_process_logging::g_num_extensions);
    writer.AddBoundary();
    writer.Flush();
  }

  unsigned extension_ids_len =
      my_strlen(child_process_logging::g_extension_ids);
  if (extension_ids_len) {
    static const char extension_msg[] = "extension-";
    static const unsigned kMaxExtensionsLen =
        kMaxReportedActiveExtensions * child_process_logging::kExtensionLen;
    writer.AddPairDataInChunks(extension_msg, sizeof(extension_msg) - 1,
        child_process_logging::g_extension_ids,
        std::min(extension_ids_len, kMaxExtensionsLen),
        child_process_logging::kExtensionLen,
        false /* Don't strip whitespace. */);
  }

  unsigned printer_info_len =
      my_strlen(child_process_logging::g_printer_info);
  if (printer_info_len) {
    static const char printer_info_msg[] = "prn-info-";
    static const unsigned kMaxPrnInfoLen =
        kMaxReportedPrinterRecords * child_process_logging::kPrinterInfoStrLen;
    writer.AddPairDataInChunks(printer_info_msg, sizeof(printer_info_msg) - 1,
        child_process_logging::g_printer_info,
        std::min(printer_info_len, kMaxPrnInfoLen),
        child_process_logging::kPrinterInfoStrLen,
        true);
  }

  if (*child_process_logging::g_num_switches) {
    writer.AddPairString("num-switches",
                         child_process_logging::g_num_switches);
    writer.AddBoundary();
    writer.Flush();
  }

  unsigned switches_len =
      my_strlen(child_process_logging::g_switches);
  if (switches_len) {
    static const char switch_msg[] = "switch-";
    static const unsigned kMaxSwitchLen =
        kMaxSwitches * child_process_logging::kSwitchLen;
    writer.AddPairDataInChunks(switch_msg, sizeof(switch_msg) - 1,
        child_process_logging::g_switches,
        std::min(switches_len, kMaxSwitchLen),
        child_process_logging::kSwitchLen,
        true /* Strip whitespace since switches are padded to kSwitchLen. */);
  }

  if (*child_process_logging::g_num_variations) {
    writer.AddPairString("num-experiments",
                         child_process_logging::g_num_variations);
    writer.AddBoundary();
    writer.Flush();
  }

  unsigned variation_chunks_len =
      my_strlen(child_process_logging::g_variation_chunks);
  if (variation_chunks_len) {
    static const char variation_msg[] = "experiment-chunk-";
    static const unsigned kMaxVariationsLen =
        kMaxReportedVariationChunks * kMaxVariationChunkSize;
    writer.AddPairDataInChunks(variation_msg, sizeof(variation_msg) - 1,
        child_process_logging::g_variation_chunks,
        std::min(variation_chunks_len, kMaxVariationsLen),
        kMaxVariationChunkSize,
        true /* Strip whitespace since variation chunks are padded. */);
  }

  if (info.oom_size) {
    char oom_size_str[kUint64StringSize];
    const unsigned oom_size_len = my_uint64_len(info.oom_size);
    my_uint64tos(oom_size_str, info.oom_size, oom_size_len);
    static const char oom_size_msg[] = "oom-size";
    writer.AddPairData(oom_size_msg, sizeof(oom_size_msg) - 1,
                       oom_size_str, oom_size_len);
    writer.AddBoundary();
    writer.Flush();
  }

  writer.AddFileContents(g_dump_msg, dump_data, dump_size);
#if defined(ADDRESS_SANITIZER)
  // Append a multipart boundary and the contents of the AddressSanitizer log.
  writer.AddBoundary();
  writer.AddFileContents(g_log_msg, log_data, log_size);
#endif
  writer.AddEnd();
  writer.Flush();

  IGNORE_RET(sys_close(temp_file_fd));

#if defined(OS_ANDROID)
  if (info.filename) {
    int filename_length = my_strlen(info.filename);

    // If this was a file, we need to copy it to the right place and use the
    // right file name so it gets uploaded by the browser.
    const char msg[] = "Output crash dump file:";
    WriteLog(msg, sizeof(msg) - 1);
    WriteLog(info.filename, filename_length - 1);

    char pid_buf[kUint64StringSize];
    uint64_t pid_str_length = my_uint64_len(info.pid);
    my_uint64tos(pid_buf, info.pid, pid_str_length);

    // -1 because we won't need the null terminator on the original filename.
    unsigned done_filename_len = filename_length - 1 + pid_str_length;
    char* done_filename = reinterpret_cast<char*>(
        allocator.Alloc(done_filename_len));
    // Rename the file such that the pid is the suffix in order signal to other
    // processes that the minidump is complete. The advantage of using the pid
    // as the suffix is that it is trivial to associate the minidump with the
    // crashed process.
    // Finally, note strncpy prevents null terminators from
    // being copied. Pad the rest with 0's.
    my_strncpy(done_filename, info.filename, done_filename_len);
    // Append the suffix a null terminator should be added.
    my_strncat(done_filename, pid_buf, pid_str_length);
    // Rename the minidump file to signal that it is complete.
    if (rename(info.filename, done_filename)) {
      const char failed_msg[] = "Failed to rename:";
      WriteLog(failed_msg, sizeof(failed_msg) - 1);
      WriteLog(info.filename, filename_length - 1);
      const char to_msg[] = "to";
      WriteLog(to_msg, sizeof(to_msg) - 1);
      WriteLog(done_filename, done_filename_len - 1);
    }
  }
#endif

  if (!info.upload)
    return;

  // The --header argument to wget looks like:
  //   --header=Content-Type: multipart/form-data; boundary=XYZ
  // where the boundary has two fewer leading '-' chars
  static const char header_msg[] =
      "--header=Content-Type: multipart/form-data; boundary=";
  char* const header = reinterpret_cast<char*>(allocator.Alloc(
      sizeof(header_msg) - 1 + sizeof(mime_boundary) - 2));
  memcpy(header, header_msg, sizeof(header_msg) - 1);
  memcpy(header + sizeof(header_msg) - 1, mime_boundary + 2,
         sizeof(mime_boundary) - 2);
  // We grab the NUL byte from the end of |mime_boundary|.

  // The --post-file argument to wget looks like:
  //   --post-file=/tmp/...
  static const char post_file_msg[] = "--post-file=";
  char* const post_file = reinterpret_cast<char*>(allocator.Alloc(
       sizeof(post_file_msg) - 1 + sizeof(temp_file)));
  memcpy(post_file, post_file_msg, sizeof(post_file_msg) - 1);
  memcpy(post_file + sizeof(post_file_msg) - 1, temp_file, sizeof(temp_file));

  const pid_t child = sys_fork();
  if (!child) {
    // Spawned helper process.
    //
    // This code is called both when a browser is crashing (in which case,
    // nothing really matters any more) and when a renderer/plugin crashes, in
    // which case we need to continue.
    //
    // Since we are a multithreaded app, if we were just to fork(), we might
    // grab file descriptors which have just been created in another thread and
    // hold them open for too long.
    //
    // Thus, we have to loop and try and close everything.
    const int fd = sys_open("/proc/self/fd", O_DIRECTORY | O_RDONLY, 0);
    if (fd < 0) {
      for (unsigned i = 3; i < 8192; ++i)
        IGNORE_RET(sys_close(i));
    } else {
      google_breakpad::DirectoryReader reader(fd);
      const char* name;
      while (reader.GetNextEntry(&name)) {
        int i;
        if (my_strtoui(&i, name) && i > 2 && i != fd)
          IGNORE_RET(sys_close(i));
        reader.PopEntry();
      }

      IGNORE_RET(sys_close(fd));
    }

    IGNORE_RET(sys_setsid());

    // Leave one end of a pipe in the wget process and watch for it getting
    // closed by the wget process exiting.
    int fds[2];
    if (sys_pipe(fds) >= 0) {
      const pid_t wget_child = sys_fork();
      if (!wget_child) {
        // Wget process.
        IGNORE_RET(sys_close(fds[0]));
        IGNORE_RET(sys_dup2(fds[1], 3));
        static const char kWgetBinary[] = "/usr/bin/wget";
        const char* args[] = {
          kWgetBinary,
          header,
          post_file,
          kUploadURL,
          "--timeout=10",  // Set a timeout so we don't hang forever.
          "--tries=1",     // Don't retry if the upload fails.
          "-O",  // output reply to fd 3
          "/dev/fd/3",
          NULL,
        };

        execve(kWgetBinary, const_cast<char**>(args), environ);
        static const char msg[] = "Cannot upload crash dump: cannot exec "
                                  "/usr/bin/wget\n";
        WriteLog(msg, sizeof(msg) - 1);
        sys__exit(1);
      }

      // Helper process.
      if (wget_child > 0) {
        IGNORE_RET(sys_close(fds[1]));
        char id_buf[17];  // Crash report IDs are expected to be 16 chars.
        ssize_t len = -1;
        // Wget should finish in about 10 seconds. Add a few more 500 ms
        // internals to account for process startup time.
        for (size_t wait_count = 0; wait_count < 24; ++wait_count) {
          struct kernel_pollfd poll_fd;
          poll_fd.fd = fds[0];
          poll_fd.events = POLLIN | POLLPRI | POLLERR;
          int ret = sys_poll(&poll_fd, 1, 500);
          if (ret < 0) {
            // Error
            break;
          } else if (ret > 0) {
            // There is data to read.
            len = HANDLE_EINTR(sys_read(fds[0], id_buf, sizeof(id_buf) - 1));
            break;
          }
          // ret == 0 -> timed out, continue waiting.
        }
        if (len > 0) {
          // Write crash dump id to stderr.
          id_buf[len] = 0;
          static const char msg[] = "\nCrash dump id: ";
          WriteLog(msg, sizeof(msg) - 1);
          WriteLog(id_buf, my_strlen(id_buf));
          WriteLog("\n", 1);

          // Write crash dump id to crash log as: seconds_since_epoch,crash_id
          struct kernel_timeval tv;
          if (g_crash_log_path && !sys_gettimeofday(&tv, NULL)) {
            uint64_t time = kernel_timeval_to_ms(&tv) / 1000;
            char time_str[kUint64StringSize];
            const unsigned time_len = my_uint64_len(time);
            my_uint64tos(time_str, time, time_len);

            int log_fd = sys_open(g_crash_log_path,
                                  O_CREAT | O_WRONLY | O_APPEND,
                                  0600);
            if (log_fd > 0) {
              sys_write(log_fd, time_str, time_len);
              sys_write(log_fd, ",", 1);
              sys_write(log_fd, id_buf, my_strlen(id_buf));
              sys_write(log_fd, "\n", 1);
              IGNORE_RET(sys_close(log_fd));
            }
          }
        }
        if (sys_waitpid(wget_child, NULL, WNOHANG) == 0) {
          // Wget process is still around, kill it.
          sys_kill(wget_child, SIGKILL);
        }
      }
    }

    // Helper process.
    IGNORE_RET(sys_unlink(info.filename));
#if defined(ADDRESS_SANITIZER)
    IGNORE_RET(sys_unlink(info.log_filename));
#endif
    IGNORE_RET(sys_unlink(temp_file));
    sys__exit(0);
  }

  // Main browser process.
  if (child <= 0)
    return;
  (void) HANDLE_EINTR(sys_waitpid(child, NULL, 0));
}

void InitCrashReporter() {
#if defined(OS_ANDROID)
  // This will guarantee that the BuildInfo has been initialized and subsequent
  // calls will not require memory allocation.
  base::android::BuildInfo::GetInstance();
#endif
  // Determine the process type and take appropriate action.
  const CommandLine& parsed_command_line = *CommandLine::ForCurrentProcess();
  if (parsed_command_line.HasSwitch(switches::kDisableBreakpad))
    return;

  const std::string process_type =
      parsed_command_line.GetSwitchValueASCII(switches::kProcessType);
  if (process_type.empty()) {
    EnableCrashDumping(getenv(env_vars::kHeadless) != NULL);
  } else if (process_type == switches::kRendererProcess ||
             process_type == switches::kPluginProcess ||
             process_type == switches::kPpapiPluginProcess ||
             process_type == switches::kZygoteProcess ||
             process_type == switches::kGpuProcess) {
#if defined(OS_ANDROID)
    NOTREACHED() << "Breakpad initialized with InitCrashReporter() instead of "
      "InitNonBrowserCrashReporter in " << process_type << " process.";
    return;
#else
    // We might be chrooted in a zygote or renderer process so we cannot call
    // GetCollectStatsConsent because that needs access the the user's home
    // dir. Instead, we set a command line flag for these processes.
    // Even though plugins are not chrooted, we share the same code path for
    // simplicity.
    if (!parsed_command_line.HasSwitch(switches::kEnableCrashReporter))
      return;
    // Get the guid and linux distro from the command line switch.
    std::string switch_value =
        parsed_command_line.GetSwitchValueASCII(switches::kEnableCrashReporter);
    size_t separator = switch_value.find(",");
    if (separator != std::string::npos) {
      child_process_logging::SetClientId(switch_value.substr(0, separator));
      base::SetLinuxDistro(switch_value.substr(separator + 1));
    } else {
      child_process_logging::SetClientId(switch_value);
    }
    EnableNonBrowserCrashDumping();
    VLOG(1) << "Non Browser crash dumping enabled for: " << process_type;
#endif  // #if defined(OS_ANDROID)
  }

  SetProcessStartTime();

  logging::SetDumpWithoutCrashingFunction(&DumpProcess);
#if defined(ADDRESS_SANITIZER)
  // Register the callback for AddressSanitizer error reporting.
  __asan_set_error_report_callback(AsanLinuxBreakpadCallback);
#endif
}

#if defined(OS_ANDROID)
void InitNonBrowserCrashReporterForAndroid(int minidump_fd) {
  const CommandLine* command_line = CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kEnableCrashReporter))
    EnableNonBrowserCrashDumping(minidump_fd);
}
#endif  // OS_ANDROID

bool IsCrashReporterEnabled() {
  return g_is_crash_reporter_enabled;
}
