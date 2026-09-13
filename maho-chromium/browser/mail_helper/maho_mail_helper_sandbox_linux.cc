// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/mail_helper/maho_mail_helper_sandbox_linux.h"

#include <atomic>
#include <errno.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "base/files/file_enumerator.h"
#include "base/files/scoped_file.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/posix/eintr_wrapper.h"
#include "base/posix/file_descriptor_shuffle.h"
#include "base/process/launch.h"
#include "sandbox/linux/bpf_dsl/bpf_dsl.h"
#include "sandbox/linux/seccomp-bpf-helpers/sigsys_handlers.h"
#include "sandbox/linux/seccomp-bpf-helpers/syscall_parameters_restrictions.h"
#include "sandbox/linux/syscall_broker/broker_command.h"
#include "sandbox/linux/syscall_broker/broker_file_permission.h"
#include "sandbox/linux/system_headers/linux_seccomp.h"
#include "sandbox/linux/system_headers/linux_syscalls.h"
#include "sandbox/policy/linux/bpf_base_policy_linux.h"
#include "sandbox/policy/linux/sandbox_linux.h"
#include "sandbox/policy/linux/sandbox_seccomp_bpf_linux.h"
#include "sandbox/policy/mojom/sandbox.mojom.h"

namespace maho::mail_helper {
namespace {

using sandbox::syscall_broker::BrokerFilePermission;
using sandbox::syscall_broker::MakeBrokerCommandSet;
using sandbox::bpf_dsl::Allow;
using sandbox::bpf_dsl::Arg;
using sandbox::bpf_dsl::Error;
using sandbox::bpf_dsl::If;
using sandbox::bpf_dsl::ResultExpr;
using sandbox::bpf_dsl::Switch;

std::atomic_bool g_force_failure_for_testing{false};
std::atomic_bool g_sandbox_engaged{false};

void AddReadOnlyPathIfPresent(
    const base::FilePath& path,
    std::vector<BrokerFilePermission>* permissions) {
  if (base::DirectoryExists(path)) {
    permissions->push_back(BrokerFilePermission::ReadOnlyRecursive(
        path.AsEndingWithSeparator().value()));
  } else if (base::PathExists(path)) {
    permissions->push_back(BrokerFilePermission::ReadOnly(path.value()));
  }
}

void AddReadOnlyNssModules(
    const base::FilePath& library_root,
    std::vector<BrokerFilePermission>* permissions) {
  if (!base::DirectoryExists(library_root)) {
    return;
  }
  base::FileEnumerator modules(
      library_root, /*recursive=*/false,
      base::FileEnumerator::FILES,
      FILE_PATH_LITERAL("libnss_*.so*"));
  for (base::FilePath module = modules.Next(); !module.empty();
       module = modules.Next()) {
    AddReadOnlyPathIfPresent(module, permissions);
  }
}

std::vector<BrokerFilePermission> BuildFilePermissions(
    const base::FilePath& mail_root) {
  std::vector<BrokerFilePermission> permissions;
  permissions.push_back(BrokerFilePermission::ReadWriteCreateRecursive(
      mail_root.AsEndingWithSeparator().value()));

  // Runtime-only reads: DNS/NSS, time zones, and system trust.
  // No path outside mail_root receives write or create permission.
  for (const char* path : {
           "/etc/hosts", "/etc/resolv.conf", "/etc/nsswitch.conf",
           "/etc/host.conf", "/etc/gai.conf", "/etc/localtime",
           "/etc/ld.so.cache", "/etc/ssl",
           "/etc/pki", "/usr/share/ca-certificates", "/usr/share/zoneinfo",
       }) {
    AddReadOnlyPathIfPresent(base::FilePath(path), &permissions);
  }
  for (const char* dir : {
           "/lib",
           "/lib64",
           "/usr/lib",
           "/usr/lib64",
           "/usr/lib/x86_64-linux-gnu",
           "/usr/lib/aarch64-linux-gnu",
       }) {
    AddReadOnlyNssModules(base::FilePath(dir), &permissions);
  }
  return permissions;
}

bool MailRootHasNoExternalLinks(const base::FilePath& mail_root) {
  base::FileEnumerator entries(
      mail_root, true,
      base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES |
          base::FileEnumerator::SHOW_SYM_LINKS,
      FILE_PATH_LITERAL("*"),
      base::FileEnumerator::FolderSearchPolicy::ALL,
      base::FileEnumerator::ErrorPolicy::STOP_ENUMERATION);
  for (base::FilePath path = entries.Next(); !path.empty();
       path = entries.Next()) {
    const auto& stat = entries.GetInfo().stat();
    if (S_ISLNK(stat.st_mode) ||
        (!S_ISDIR(stat.st_mode) && stat.st_nlink != 1)) {
      return false;
    }
  }
  return entries.GetError() == base::File::FILE_OK;
}

ResultExpr RestrictMailSocket() {
  const Arg<int> domain(0);
  const Arg<int> type(1);
  const Arg<int> protocol(2);
  constexpr int kTypeFlags = SOCK_NONBLOCK | SOCK_CLOEXEC;

  const ResultExpr inet_socket =
      Switch(type & ~kTypeFlags)
          .Case(SOCK_STREAM,
                If(protocol == 0, Allow())
                    .Else(If(protocol == IPPROTO_TCP, Allow())
                              .Else(sandbox::CrashSIGSYSSocket())))
          .Case(SOCK_DGRAM,
                If(protocol == 0, Allow())
                    .Else(If(protocol == IPPROTO_UDP, Allow())
                              .Else(sandbox::CrashSIGSYSSocket())))
          .Default(sandbox::CrashSIGSYSSocket());

  // glibc's getaddrinfo(AI_ADDRCONFIG) enumerates interface addresses over a
  // NETLINK_ROUTE socket (same allowance the network-service policy grants
  // AddressTrackerLinux). Only raw NETLINK_ROUTE sockets are permitted.
  ResultExpr netlink_type_switch =
      Switch(type & ~kTypeFlags)
          .Case(SOCK_RAW, Switch(protocol)
                              .Case(NETLINK_ROUTE, Allow())
                              .Default(sandbox::CrashSIGSYSSocket()))
          .Default(sandbox::CrashSIGSYSSocket());

  return Switch(domain)
      .Case(AF_UNIX, Error(EPERM))
      .Cases({AF_INET, AF_INET6}, inet_socket)
      .Case(AF_NETLINK, netlink_type_switch)
      .Default(sandbox::CrashSIGSYSSocket());
}

ResultExpr RestrictMailSocketPair() {
  const Arg<int> domain(0);
  const Arg<int> type(1);
  const Arg<int> protocol(2);
  constexpr int kTypeFlags = SOCK_NONBLOCK | SOCK_CLOEXEC;

  // Mojo may create connected local transports after sandbox engagement.
  // Stream socketpairs have no pathname and cannot be retargeted to another
  // user's session endpoint, unlike a newly created AF_UNIX client socket.
  // SOCK_SEQPACKET pairs are equally anonymous: base::CreateSocketPair (used
  // by the signal-based file broker for its per-request reply channel and by
  // Chromium IPC helpers) requests SOCK_SEQPACKET on Linux, so the broker's
  // requests could never leave the trap without this allowance.
  return If(domain == AF_UNIX,
            If((type & ~kTypeFlags) == SOCK_STREAM,
               If(protocol == 0, Allow()).Else(Error(EPERM)))
                .Else(If((type & ~kTypeFlags) == SOCK_SEQPACKET,
                         If(protocol == 0, Allow()).Else(Error(EPERM)))
                          .Else(Error(EPERM))))
      .Else(Error(EPERM));
}

ResultExpr RestrictMailGetSockopt() {
  const Arg<int> level(1);
  const Arg<int> option(2);
  ResultExpr socket_options =
      Switch(option)
          .Cases({SO_ERROR, SO_TYPE, SO_RCVBUF, SO_SNDBUF}, Allow())
#ifdef SO_PEERCRED
          .Case(SO_PEERCRED, Allow())
#endif
          .Default(sandbox::CrashSIGSYSSockopt());
  return Switch(level)
      .Case(SOL_SOCKET, socket_options)
      .Case(IPPROTO_TCP,
            Switch(option)
                .Case(TCP_INFO, Allow())
                .Default(sandbox::CrashSIGSYSSockopt()))
      .Default(sandbox::CrashSIGSYSSockopt());
}

ResultExpr RestrictMailSetSockopt() {
  const Arg<int> level(1);
  const Arg<int> option(2);
  ResultExpr policy =
      Switch(level)
          .Case(SOL_SOCKET,
                Switch(option)
                    // Rust callers configure socket read/write timeouts:
                    // imap_client connect_with_timeout + OAuth accepted streams.
                    .Cases({SO_KEEPALIVE, SO_RCVBUF, SO_SNDBUF, SO_RCVTIMEO,
                            SO_SNDTIMEO},
                           Allow())
                    .Default(sandbox::CrashSIGSYSSockopt()))
          .Case(IPPROTO_TCP,
                Switch(option)
                    .Cases({TCP_KEEPIDLE, TCP_KEEPINTVL, TCP_NODELAY}, Allow())
                    .Default(sandbox::CrashSIGSYSSockopt()))
          .Default(sandbox::CrashSIGSYSSockopt());
#ifdef IP_RECVERR
  policy = If(level == IPPROTO_IP,
              If(option == IP_RECVERR, Allow())
                  .Else(sandbox::CrashSIGSYSSockopt()))
               .Else(policy);
#endif
#ifdef IPV6_RECVERR
  policy = If(level == IPPROTO_IPV6,
              If(option == IPV6_RECVERR, Allow())
                  .Else(sandbox::CrashSIGSYSSockopt()))
               .Else(policy);
#endif
  return policy;
}

class MailHelperPolicy final : public sandbox::policy::BPFBasePolicy {
 public:
  explicit MailHelperPolicy(int oauth_loopback_listener_fd)
      : oauth_loopback_listener_fd_(oauth_loopback_listener_fd) {}

  ResultExpr EvaluateSyscall(int sysno) const override {
    auto* sandbox_linux = sandbox::policy::SandboxLinux::GetInstance();
    if (sandbox_linux->ShouldBrokerHandleSyscall(sysno)) {
      return sandbox_linux->HandleViaBroker(sysno);
    }

    switch (sysno) {
      case __NR_sendmsg:
      case __NR_recvmsg:
        // The signal-based file broker communicates over its socketpair with
        // sendmsg(2)/recvmsg(2) from inside the SIGSYS trap handler. Without
        // these the baseline policy's CrashSIGSYS default breaks every
        // brokered filesystem operation (-ENOMEM). The process cannot create
        // AF_UNIX sockets after sandbox entry, so these only ever target fds
        // it legitimately holds: the broker channel, Mojo transports, the
        // prebound OAuth listener, and post-entry AF_INET mail sockets.
        return Allow();
      case __NR_socket:
        return RestrictMailSocket();
      case __NR_socketpair:
        return RestrictMailSocketPair();
      case __NR_connect:
        // Only AF_INET/AF_INET6 sockets can be created after sandbox entry.
        // Startup closes every inherited descriptor except the already-
        // connected Mojo transport and prebound loopback OAuth listener, so
        // connect() has no unconnected AF_UNIX capability to operate on.
        return Allow();
      case __NR_getpeername:
      case __NR_getsockname:
      case __NR_shutdown:
        return Allow();
      case __NR_getsockopt:
        return RestrictMailGetSockopt();
      case __NR_setsockopt:
        return RestrictMailSetSockopt();
      case __NR_bind:
      case __NR_listen:
        return Error(EPERM);
#if defined(__NR_accept)
      case __NR_accept:
#endif
      case __NR_accept4:
        if (oauth_loopback_listener_fd_ < 0) {
          return Error(EPERM);
        }
        return If(Arg<int>(0) == oauth_loopback_listener_fd_, Allow())
            .Else(Error(EPERM));
      case __NR_ptrace:
        return sandbox::CrashSIGSYSPtrace();
#if defined(__NR_fork)
      case __NR_fork:
#endif
#if defined(__NR_vfork)
      case __NR_vfork:
#endif
      case __NR_execve:
#if defined(__NR_execveat)
      case __NR_execveat:
#endif
        return Error(EPERM);
      case __NR_clone:
        return sandbox::RestrictCloneToThreadsAndEPERMFork();
#if defined(__NR_clone3)
      case __NR_clone3:
        return Error(ENOSYS);
#endif
      case __NR_fcntl:
        return sandbox::RestrictFcntlCommands();
      case __NR_ioctl:
        return sandbox::RestrictIoctl();
      case __NR_fsync:
      case __NR_fdatasync:
      case __NR_pread64:
      case __NR_pwrite64:
      case __NR_mremap:
      case __NR_getdents64:
        return Allow();
      case __NR_prctl: {
        const Arg<int> option(0);
        return If(option == PR_GET_SECCOMP, Allow())
            .Else(BPFBasePolicy::EvaluateSyscall(sysno));
      }
      default:
        return BPFBasePolicy::EvaluateSyscall(sysno);
    }
  }

 private:
  const int oauth_loopback_listener_fd_;
};

bool SeccompFilterIsActive() {
  return prctl(PR_GET_SECCOMP, 0, 0, 0, 0) == SECCOMP_MODE_FILTER;
}

}  // namespace

bool CloseLinuxMailHelperInheritedFds(int mojo_fd,
                                      int oauth_loopback_listener_fd) {
  if (mojo_fd < 0) {
    return false;
  }
  if (oauth_loopback_listener_fd >= 0 &&
      mojo_fd == oauth_loopback_listener_fd) {
    return false;
  }
  base::InjectiveMultimap preserved_fds = {
      base::InjectionArc(mojo_fd, mojo_fd, false),
  };
  if (oauth_loopback_listener_fd >= 0) {
    preserved_fds.push_back(base::InjectionArc(
        oauth_loopback_listener_fd, oauth_loopback_listener_fd, false));
  }
  base::CloseSuperfluousFds(preserved_fds);
  if (fcntl(mojo_fd, F_GETFD) < 0) {
    return false;
  }
  if (oauth_loopback_listener_fd >= 0 &&
      fcntl(oauth_loopback_listener_fd, F_GETFD) < 0) {
    return false;
  }
  return true;
}

bool InitializeLinuxMailHelperSandbox(const base::FilePath& mail_root,
                                      int oauth_loopback_listener_fd) {
  if (g_force_failure_for_testing.load(std::memory_order_relaxed)) {
    LOG(ERROR) << "[MahoMailHelper] Forced Linux sandbox failure.";
    return false;
  }
  if (mail_root.empty() || !mail_root.IsAbsolute() ||
      !base::DirectoryExists(mail_root)) {
    LOG(ERROR) << "[MahoMailHelper] Linux sandbox requires an existing, "
                  "absolute Mail root.";
    return false;
  }

  base::FilePath canonical_root;
  if (!base::NormalizeFilePath(mail_root, &canonical_root) ||
      canonical_root != mail_root.StripTrailingSeparators()) {
    LOG(ERROR) << "[MahoMailHelper] Linux Mail root is not canonical.";
    return false;
  }
  if (!MailRootHasNoExternalLinks(canonical_root)) {
    LOG(ERROR) << "[MahoMailHelper] Linux Mail root contains an external "
                  "link or cannot be enumerated safely.";
    return false;
  }

  auto* sandbox_linux = sandbox::policy::SandboxLinux::GetInstance();
  sandbox_linux->PreinitializeSandbox();

  sandbox::policy::SandboxLinux::Options options;
  // This helper deliberately has no namespace/chroot layer: Mail requires the
  // profile-local SQLCipher DB plus system DNS and trust files through the
  // broker. The open-directory check protects a namespace
  // root that is not engaged here, so disable that check explicitly.
  options.check_for_open_directories = false;
  sandbox_linux->StartBrokerProcess(
      MakeBrokerCommandSet({
          sandbox::syscall_broker::COMMAND_ACCESS,
          sandbox::syscall_broker::COMMAND_MKDIR,
          sandbox::syscall_broker::COMMAND_OPEN,
          sandbox::syscall_broker::COMMAND_READLINK,
          sandbox::syscall_broker::COMMAND_RENAME,
          sandbox::syscall_broker::COMMAND_RMDIR,
          sandbox::syscall_broker::COMMAND_STAT,
          sandbox::syscall_broker::COMMAND_STAT64,
          sandbox::syscall_broker::COMMAND_UNLINK,
      }),
      BuildFilePermissions(canonical_root), options);
  const std::vector<int> sandbox_fds =
      sandbox_linux->GetFileDescriptorsToClose();
  base::ScopedFD proc_fd;
  if (!sandbox_fds.empty()) {
    proc_fd.reset(HANDLE_EINTR(dup(sandbox_fds.front())));
  }
  if (!proc_fd.is_valid()) {
    PLOG(ERROR) << "[MahoMailHelper] Cannot duplicate /proc for seccomp.";
    return false;
  }
  // kNoSandbox is used only to seal SandboxLinux state and close inherited
  // broker/proc descriptors before installing the Mail-specific external BPF
  // policy below. It does not claim a namespace or built-in seccomp policy.
  if (!sandbox_linux->InitializeSandbox(sandbox::mojom::Sandbox::kNoSandbox,
                                        {}, options)) {
    LOG(ERROR) << "[MahoMailHelper] Linux sandbox state did not seal.";
    return false;
  }

  const bool initialized =
      sandbox::policy::SandboxSeccompBPF::StartSandboxWithExternalPolicy(
          std::make_unique<MailHelperPolicy>(oauth_loopback_listener_fd),
          std::move(proc_fd));
  if (!initialized || !SeccompFilterIsActive()) {
    LOG(ERROR) << "[MahoMailHelper] Mail-specific seccomp did not engage.";
    return false;
  }
  g_sandbox_engaged.store(true, std::memory_order_release);
  LOG(INFO) << "[MahoMailHelper] Linux seccomp/file broker active.";
  return true;
}

bool IsLinuxMailHelperSandboxEngagedForTesting() {
  return g_sandbox_engaged.load(std::memory_order_acquire);
}

void SetLinuxMailHelperSandboxFailureForTesting(bool fail) {
  g_force_failure_for_testing.store(fail, std::memory_order_relaxed);
}

}  // namespace maho::mail_helper
