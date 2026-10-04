// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/mail_helper/maho_mail_helper_sandbox_linux.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cstdio>
#include <dlfcn.h>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_util.h"
#include "base/threading/thread.h"
#include "sandbox/linux/system_headers/linux_seccomp.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace maho::mail_helper {
namespace {

constexpr int kSandboxInitializationFailed = 90;
constexpr int kSandboxProbeFailed = 91;
constexpr int kExpectedErrnoMissing = 92;

struct ChildResult {
  bool exited = false;
  int exit_code = -1;
  int signal = 0;
};

template <typename Callback>
ChildResult RunSandboxedChild(const base::FilePath& mail_root,
                              Callback callback,
                              int oauth_loopback_listener_fd = -1) {
  const pid_t child = fork();
  if (child < 0) {
    ADD_FAILURE() << "fork failed: " << errno;
    return {};
  }
  if (child == 0) {
    alarm(10);
    if (!InitializeLinuxMailHelperSandbox(mail_root,
                                          oauth_loopback_listener_fd)) {
      _exit(kSandboxInitializationFailed);
    }
    _exit(callback());
  }

  int status = 0;
  if (waitpid(child, &status, 0) != child) {
    ADD_FAILURE() << "waitpid failed: " << errno;
    return {};
  }
  if (WIFSIGNALED(status)) {
    return {.signal = WTERMSIG(status)};
  }
  if (!WIFEXITED(status)) {
    return {};
  }
  return {.exited = true, .exit_code = WEXITSTATUS(status)};
}

base::FilePath CreateMailRoot(base::ScopedTempDir* temp) {
  EXPECT_TRUE(temp->CreateUniqueTempDir());
  const base::FilePath mail_root = temp->GetPath().AppendASCII("MahoMail");
  EXPECT_TRUE(base::CreateDirectory(mail_root));
  return mail_root;
}

int RequireErrno(int expected_errno, int result) {
  return result == -1 && errno == expected_errno ? 0 : kExpectedErrnoMissing;
}

int EnsureNonStdioFd(int fd) {
  if (fd < 0 || fd > STDERR_FILENO) {
    return fd;
  }
  const int duplicated = fcntl(fd, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
  close(fd);
  return duplicated;
}

TEST(MahoMailHelperSandboxLinuxTest, ReportsBrokerAndSeccompEngaged) {
  base::ScopedTempDir temp;
  const base::FilePath mail_root = CreateMailRoot(&temp);

  const ChildResult result = RunSandboxedChild(mail_root, [] {
    if (!IsLinuxMailHelperSandboxEngagedForTesting()) {
      return kSandboxProbeFailed;
    }
    return prctl(PR_GET_SECCOMP, 0, 0, 0, 0) == SECCOMP_MODE_FILTER
               ? 0
               : kSandboxProbeFailed;
  });
  EXPECT_TRUE(result.exited);
  EXPECT_EQ(0, result.exit_code);

}

TEST(MahoMailHelperSandboxLinuxTest, AllowsMailRootSqlitePrimitives) {
  base::ScopedTempDir temp;
  const base::FilePath mail_root = CreateMailRoot(&temp);

  const ChildResult result = RunSandboxedChild(mail_root, [&] {
    const base::FilePath db = mail_root.AppendASCII("maho_mail.db");
    const base::FilePath wal = mail_root.AppendASCII("maho_mail.db-wal");
    int fd = open(db.value().c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (fd < 0 || write(fd, "sqlite", 6) != 6 || fsync(fd) != 0 ||
        close(fd) != 0) {
      return kSandboxProbeFailed;
    }
    if (rename(db.value().c_str(), wal.value().c_str()) != 0) {
      return kSandboxProbeFailed;
    }
    return unlink(wal.value().c_str()) == 0 ? 0 : kSandboxProbeFailed;
  });
  EXPECT_TRUE(result.exited);
  EXPECT_EQ(0, result.exit_code);
}

TEST(MahoMailHelperSandboxLinuxTest, DeniesWritesOutsideMailRootWithEacces) {
  base::ScopedTempDir temp;
  const base::FilePath mail_root = CreateMailRoot(&temp);
  const base::FilePath outside = temp.GetPath().AppendASCII("outside.db");

  ChildResult result = RunSandboxedChild(mail_root, [&] {
    errno = 0;
    return RequireErrno(
        EACCES,
        open(outside.value().c_str(), O_CREAT | O_WRONLY | O_CLOEXEC, 0600));
  });
  EXPECT_TRUE(result.exited);
  EXPECT_EQ(0, result.exit_code);

  result = RunSandboxedChild(mail_root, [] {
    errno = 0;
    return RequireErrno(EACCES, open("/etc/passwd", O_WRONLY | O_CLOEXEC));
  });
  EXPECT_TRUE(result.exited);
  EXPECT_EQ(0, result.exit_code);

  const base::FilePath source = mail_root.AppendASCII("source.sqlite");
  ASSERT_TRUE(base::WriteFile(source, "mail"));
  result = RunSandboxedChild(mail_root, [&] {
    errno = 0;
    return RequireErrno(
        EACCES, rename(source.value().c_str(), outside.value().c_str()));
  });
  EXPECT_TRUE(result.exited);
  EXPECT_EQ(0, result.exit_code);

  const base::FilePath outside_dir =
      temp.GetPath().AppendASCII("outside-dir");
  const base::FilePath outside_empty =
      temp.GetPath().AppendASCII("outside-empty");
  const base::FilePath outside_file = outside_dir.AppendASCII("file");
  const base::FilePath outside_new =
      temp.GetPath().AppendASCII("outside-new");
  ASSERT_TRUE(base::CreateDirectory(outside_dir));
  ASSERT_TRUE(base::CreateDirectory(outside_empty));
  ASSERT_TRUE(base::WriteFile(outside_file, "outside"));
  result = RunSandboxedChild(mail_root, [&] {
    errno = 0;
    if (RequireErrno(EACCES, mkdir(outside_new.value().c_str(), 0700)) != 0) {
      return kSandboxProbeFailed;
    }
    errno = 0;
    if (RequireErrno(EACCES, unlink(outside_file.value().c_str())) != 0) {
      return kSandboxProbeFailed;
    }
    errno = 0;
    return RequireErrno(EACCES, rmdir(outside_empty.value().c_str()));
  });
  EXPECT_TRUE(result.exited);
  EXPECT_EQ(0, result.exit_code);
  EXPECT_TRUE(base::PathExists(outside_file));
  EXPECT_TRUE(base::DirectoryExists(outside_empty));
}

TEST(MahoMailHelperSandboxLinuxTest, AllowsTcpImapAndSmtpTransport) {
  base::ScopedTempDir temp;
  const base::FilePath mail_root = CreateMailRoot(&temp);

  int listener = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
  ASSERT_GE(listener, 0);
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_EQ(0, bind(listener, reinterpret_cast<sockaddr*>(&address),
                    sizeof(address)));
  ASSERT_EQ(0, listen(listener, 1));
  socklen_t address_length = sizeof(address);
  ASSERT_EQ(0, getsockname(listener, reinterpret_cast<sockaddr*>(&address),
                           &address_length));

  const ChildResult result = RunSandboxedChild(mail_root, [&] {
    int client = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
    if (client < 0) {
      return kSandboxProbeFailed;
    }
    const int connected =
        connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    close(client);
    return connected == 0 ? 0 : kSandboxProbeFailed;
  });
  close(listener);
  EXPECT_TRUE(result.exited);
  EXPECT_EQ(0, result.exit_code);
}

TEST(MahoMailHelperSandboxLinuxTest, ResolvesLocalhostAfterSandboxEngagement) {
  base::ScopedTempDir temp;
  const base::FilePath mail_root = CreateMailRoot(&temp);

  const ChildResult result = RunSandboxedChild(mail_root, [] {
    addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    if (getaddrinfo("localhost", "143", &hints, &addresses) != 0 ||
        addresses == nullptr) {
      return kSandboxProbeFailed;
    }
    bool found_loopback = false;
    for (addrinfo* current = addresses; current; current = current->ai_next) {
      if (current->ai_family == AF_INET) {
        const auto* address =
            reinterpret_cast<const sockaddr_in*>(current->ai_addr);
        found_loopback |= ntohl(address->sin_addr.s_addr) == INADDR_LOOPBACK;
      } else if (current->ai_family == AF_INET6) {
        const auto* address =
            reinterpret_cast<const sockaddr_in6*>(current->ai_addr);
        found_loopback |= IN6_IS_ADDR_LOOPBACK(&address->sin6_addr);
      }
    }
    freeaddrinfo(addresses);
    return found_loopback ? 0 : kSandboxProbeFailed;
  });
  EXPECT_TRUE(result.exited);
  EXPECT_EQ(0, result.exit_code);
}

TEST(MahoMailHelperSandboxLinuxTest, LoadsNssModuleAfterSandboxEngagement) {
  base::ScopedTempDir temp;
  const base::FilePath mail_root = CreateMailRoot(&temp);

  const ChildResult result = RunSandboxedChild(mail_root, [] {
    void* module = dlopen("libnss_dns.so.2", RTLD_NOW | RTLD_LOCAL);
    if (!module) {
      return kSandboxProbeFailed;
    }
    return dlclose(module) == 0 ? 0 : kSandboxProbeFailed;
  });
  EXPECT_TRUE(result.exited);
  EXPECT_EQ(0, result.exit_code);
}

TEST(MahoMailHelperSandboxLinuxTest, AllowsDnsUdpAfterSandboxEngagement) {
  base::ScopedTempDir temp;
  const base::FilePath mail_root = CreateMailRoot(&temp);

  int dns_server = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP);
  ASSERT_GE(dns_server, 0);
  sockaddr_in server_address = {};
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_EQ(0, bind(dns_server, reinterpret_cast<sockaddr*>(&server_address),
                    sizeof(server_address)));
  socklen_t server_length = sizeof(server_address);
  ASSERT_EQ(0, getsockname(dns_server,
                           reinterpret_cast<sockaddr*>(&server_address),
                           &server_length));

  std::array<int, 2> ready_pipe;
  ASSERT_EQ(0, pipe2(ready_pipe.data(), O_CLOEXEC));
  const pid_t child = fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    close(ready_pipe[0]);
    if (!InitializeLinuxMailHelperSandbox(mail_root) ||
        !IsLinuxMailHelperSandboxEngagedForTesting()) {
      _exit(kSandboxInitializationFailed);
    }
    const char ready = 'R';
    if (write(ready_pipe[1], &ready, sizeof(ready)) != sizeof(ready)) {
      _exit(kSandboxProbeFailed);
    }
    close(ready_pipe[1]);

    int client = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP);
    if (client < 0) {
      _exit(kSandboxProbeFailed);
    }
    const std::array<unsigned char, 19> query = {
        0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x01, 'a',  0x00, 0x00, 0x01, 0x00, 0x01};
    if (sendto(client, query.data(), query.size(), 0,
               reinterpret_cast<sockaddr*>(&server_address),
               sizeof(server_address)) != static_cast<ssize_t>(query.size())) {
      _exit(kSandboxProbeFailed);
    }
    std::array<unsigned char, 12> response = {};
    const ssize_t received = recv(client, response.data(), response.size(), 0);
    close(client);
    _exit(received == static_cast<ssize_t>(response.size()) &&
                  response[0] == 0x12 && response[1] == 0x34 &&
                  response[2] == 0x81 && response[3] == 0x80
              ? 0
              : kSandboxProbeFailed);
  }

  close(ready_pipe[1]);
  pollfd ready_event = {.fd = ready_pipe[0], .events = POLLIN};
  ASSERT_EQ(1, poll(&ready_event, 1, 5000));
  ASSERT_NE(0, ready_event.revents & (POLLIN | POLLHUP));
  char ready = 0;
  ASSERT_EQ(static_cast<ssize_t>(sizeof(ready)),
            read(ready_pipe[0], &ready, sizeof(ready)));
  close(ready_pipe[0]);
  ASSERT_EQ('R', ready);

  pollfd dns_event = {.fd = dns_server, .events = POLLIN};
  ASSERT_EQ(1, poll(&dns_event, 1, 5000));
  ASSERT_NE(0, dns_event.revents & POLLIN);
  std::array<unsigned char, 512> query = {};
  sockaddr_in client_address = {};
  socklen_t client_length = sizeof(client_address);
  const ssize_t query_length =
      recvfrom(dns_server, query.data(), query.size(), 0,
               reinterpret_cast<sockaddr*>(&client_address), &client_length);
  ASSERT_GT(query_length, 12);
  const std::array<unsigned char, 12> response = {
      query[0], query[1], 0x81, 0x80, 0x00, 0x01,
      0x00,     0x00,     0x00, 0x00, 0x00, 0x00};
  ASSERT_EQ(static_cast<ssize_t>(response.size()),
            sendto(dns_server, response.data(), response.size(), 0,
                   reinterpret_cast<sockaddr*>(&client_address),
                   client_length));
  close(dns_server);

  int status = 0;
  ASSERT_EQ(child, waitpid(child, &status, 0));
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(0, WEXITSTATUS(status));
}

TEST(MahoMailHelperSandboxLinuxTest,
     AllowsResolverErrorQueueOptionsAfterSandboxEngagement) {
  base::ScopedTempDir temp;
  const base::FilePath mail_root = CreateMailRoot(&temp);

  const ChildResult result = RunSandboxedChild(mail_root, [] {
    int enabled = 1;
#ifdef IP_RECVERR
    int ipv4 = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP);
    if (ipv4 < 0 ||
        setsockopt(ipv4, IPPROTO_IP, IP_RECVERR, &enabled,
                   sizeof(enabled)) != 0) {
      return kSandboxProbeFailed;
    }
    close(ipv4);
#endif
#ifdef IPV6_RECVERR
    int ipv6 = socket(AF_INET6, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP);
    if (ipv6 < 0 ||
        setsockopt(ipv6, IPPROTO_IPV6, IPV6_RECVERR, &enabled,
                   sizeof(enabled)) != 0) {
      return kSandboxProbeFailed;
    }
    close(ipv6);
#endif
    return 0;
  });
  EXPECT_TRUE(result.exited);
  EXPECT_EQ(0, result.exit_code);
}

TEST(MahoMailHelperSandboxLinuxTest,
     AllowsSocketTimeoutOptionsAfterSandboxEngagement) {
  base::ScopedTempDir temp;
  const base::FilePath mail_root = CreateMailRoot(&temp);

  const ChildResult result = RunSandboxedChild(mail_root, [] {
    int socket_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
    if (socket_fd < 0) {
      return kSandboxProbeFailed;
    }
    struct timeval timeout = {.tv_sec = 15, .tv_usec = 0};
    if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                   sizeof(timeout)) != 0 ||
        setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                   sizeof(timeout)) != 0) {
      close(socket_fd);
      return kSandboxProbeFailed;
    }
    close(socket_fd);

    int ipv6_socket = socket(AF_INET6, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
    if (ipv6_socket >= 0) {
      if (setsockopt(ipv6_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                     sizeof(timeout)) != 0 ||
          setsockopt(ipv6_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                     sizeof(timeout)) != 0) {
        close(ipv6_socket);
        return kSandboxProbeFailed;
      }
      close(ipv6_socket);
    }
    return 0;
  });
  EXPECT_TRUE(result.exited);
  EXPECT_EQ(0, result.exit_code);
}

TEST(MahoMailHelperSandboxLinuxTest,
     DeniesArbitraryUnixSocketTransportWithEperm) {
  base::ScopedTempDir temp;
  const base::FilePath mail_root = CreateMailRoot(&temp);

  const base::FilePath socket_path = temp.GetPath().AppendASCII("unrelated-ipc");
  int listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  ASSERT_GE(listener, 0);
  sockaddr_un address = {};
  address.sun_family = AF_UNIX;
  ASSERT_LT(socket_path.value().size(), sizeof(address.sun_path));
  base::strlcpy(base::span(address.sun_path), socket_path.value());
  ASSERT_EQ(0, bind(listener, reinterpret_cast<sockaddr*>(&address),
                    sizeof(address)));
  ASSERT_EQ(0, listen(listener, 1));

  const ChildResult result = RunSandboxedChild(mail_root, [] {
    errno = 0;
    return RequireErrno(
        EPERM, socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
  });
  close(listener);
  unlink(socket_path.value().c_str());
  EXPECT_TRUE(result.exited);
  EXPECT_EQ(0, result.exit_code);
}

TEST(MahoMailHelperSandboxLinuxTest,
     AllowsConnectedUnixStreamSocketPairsForMojoOnly) {
  base::ScopedTempDir temp;
  const base::FilePath mail_root = CreateMailRoot(&temp);

  const ChildResult result = RunSandboxedChild(mail_root, [] {
    std::array<int, 2> transport;
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0,
                   transport.data()) != 0) {
      return kSandboxProbeFailed;
    }
    const char sent = 'M';
    char received = 0;
    const bool exchanged =
        write(transport[0], &sent, sizeof(sent)) == sizeof(sent) &&
        read(transport[1], &received, sizeof(received)) == sizeof(received) &&
        received == sent;
    close(transport[0]);
    close(transport[1]);
    if (!exchanged) {
      return kSandboxProbeFailed;
    }

    std::array<int, 2> datagram;
    errno = 0;
    return RequireErrno(
        EPERM, socketpair(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0,
                          datagram.data()));
  });
  EXPECT_TRUE(result.exited);
  EXPECT_EQ(0, result.exit_code);
}

TEST(MahoMailHelperSandboxLinuxTest, DeniesProcessCreationAndExecWithEperm) {
  base::ScopedTempDir temp;
  const base::FilePath mail_root = CreateMailRoot(&temp);

  ChildResult result = RunSandboxedChild(mail_root, [] {
    errno = 0;
    return RequireErrno(EPERM, fork());
  });
  EXPECT_TRUE(result.exited);
  EXPECT_EQ(0, result.exit_code);

  result = RunSandboxedChild(mail_root, [] {
    char* const argv[] = {const_cast<char*>("true"), nullptr};
    errno = 0;
    return RequireErrno(EPERM, execv("/bin/true", argv));
  });
  EXPECT_TRUE(result.exited);
  EXPECT_EQ(0, result.exit_code);
}

TEST(MahoMailHelperSandboxLinuxTest, AllowsThreadCreationAfterSandboxEngagement) {
  base::ScopedTempDir temp;
  const base::FilePath mail_root = CreateMailRoot(&temp);

  const ChildResult result = RunSandboxedChild(mail_root, [] {
    base::Thread thread("mail-sandbox-thread");
    if (!thread.Start()) {
      return kSandboxProbeFailed;
    }
    thread.Stop();
    return 0;
  });
  EXPECT_TRUE(result.exited);
  EXPECT_EQ(0, result.exit_code);
}

TEST(MahoMailHelperSandboxLinuxTest,
     TerminatesPtraceAndRawSocketAttemptsWithSigsegv) {
  base::ScopedTempDir temp;
  const base::FilePath mail_root = CreateMailRoot(&temp);

  ChildResult result = RunSandboxedChild(mail_root, [] {
    return ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) == -1
               ? kSandboxProbeFailed
               : 0;
  });
  EXPECT_FALSE(result.exited);
  EXPECT_EQ(SIGSEGV, result.signal);

  result = RunSandboxedChild(mail_root, [] {
    return socket(AF_INET, SOCK_RAW | SOCK_CLOEXEC, IPPROTO_RAW) == -1
               ? kSandboxProbeFailed
               : 0;
  });
  EXPECT_FALSE(result.exited);
  EXPECT_EQ(SIGSEGV, result.signal);
}

TEST(MahoMailHelperSandboxLinuxTest, DeniesServerSocketSyscallsWithEperm) {
  base::ScopedTempDir temp;
  const base::FilePath mail_root = CreateMailRoot(&temp);

  const ChildResult result = RunSandboxedChild(mail_root, [] {
    int socket_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
    if (socket_fd < 0) {
      return kSandboxProbeFailed;
    }
    errno = 0;
    const int outcome = RequireErrno(EPERM, listen(socket_fd, 1));
    close(socket_fd);
    return outcome;
  });
  EXPECT_TRUE(result.exited);
  EXPECT_EQ(0, result.exit_code);
}

TEST(MahoMailHelperSandboxLinuxTest, AllowsAcceptOnPreboundLoopbackOnly) {
  base::ScopedTempDir temp;
  const base::FilePath mail_root = CreateMailRoot(&temp);
  int listener = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
                        IPPROTO_TCP);
  ASSERT_GE(listener, 0);
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ASSERT_EQ(0, bind(listener, reinterpret_cast<sockaddr*>(&address),
                    sizeof(address)));
  ASSERT_EQ(0, listen(listener, 1));

  ChildResult result = RunSandboxedChild(mail_root, [=] {
    errno = 0;
    return RequireErrno(EAGAIN,
                        accept4(listener, nullptr, nullptr, SOCK_CLOEXEC));
  }, listener);
  EXPECT_TRUE(result.exited);
  EXPECT_EQ(0, result.exit_code);

  result = RunSandboxedChild(mail_root, [=] {
    errno = 0;
    return RequireErrno(EPERM,
                        accept4(listener, nullptr, nullptr, SOCK_CLOEXEC));
  });
  EXPECT_TRUE(result.exited);
  EXPECT_EQ(0, result.exit_code);
  close(listener);
}

TEST(MahoMailHelperSandboxLinuxTest, ForcedInitializationFailureFailsClosed) {
  base::ScopedTempDir temp;
  const base::FilePath mail_root = CreateMailRoot(&temp);
  SetLinuxMailHelperSandboxFailureForTesting(true);
  EXPECT_FALSE(InitializeLinuxMailHelperSandbox(mail_root));
  EXPECT_FALSE(IsLinuxMailHelperSandboxEngagedForTesting());
  SetLinuxMailHelperSandboxFailureForTesting(false);
}

TEST(MahoMailHelperSandboxLinuxTest, ClosesUnlistedInheritedDescriptors) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  const base::FilePath outside = temp.GetPath().AppendASCII("outside");
  int mojo_fd =
      EnsureNonStdioFd(open("/dev/null", O_RDONLY | O_CLOEXEC));
  int oauth_fd =
      EnsureNonStdioFd(open("/dev/null", O_RDONLY | O_CLOEXEC));
  int inherited_fd = EnsureNonStdioFd(
      open(outside.value().c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600));
  ASSERT_GE(mojo_fd, 0);
  ASSERT_GE(oauth_fd, 0);
  ASSERT_GE(inherited_fd, 0);

  const pid_t child = fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    if (!CloseLinuxMailHelperInheritedFds(mojo_fd, oauth_fd) ||
        fcntl(mojo_fd, F_GETFD) < 0 || fcntl(oauth_fd, F_GETFD) < 0) {
      _exit(kSandboxProbeFailed);
    }
    errno = 0;
    if (write(inherited_fd, "x", 1) != -1 || errno != EBADF) {
      _exit(kSandboxProbeFailed);
    }
    _exit(0);
  }

  close(mojo_fd);
  close(oauth_fd);
  close(inherited_fd);
  int status = 0;
  ASSERT_EQ(child, waitpid(child, &status, 0));
  EXPECT_TRUE(WIFEXITED(status));
  EXPECT_EQ(0, WEXITSTATUS(status));
}

TEST(MahoMailHelperSandboxLinuxTest, NonCanonicalMailRootFailsClosed) {
  base::ScopedTempDir temp;
  const base::FilePath mail_root = CreateMailRoot(&temp);
  const base::FilePath alias = temp.GetPath().AppendASCII("mail-link");
  ASSERT_TRUE(base::CreateSymbolicLink(mail_root, alias));

  EXPECT_FALSE(InitializeLinuxMailHelperSandbox(alias));
  EXPECT_FALSE(IsLinuxMailHelperSandboxEngagedForTesting());
}

TEST(MahoMailHelperSandboxLinuxTest, SymlinkInsideMailRootFailsClosed) {
  base::ScopedTempDir temp;
  const base::FilePath mail_root = CreateMailRoot(&temp);
  const base::FilePath escape = mail_root.AppendASCII("escape");
  ASSERT_TRUE(base::CreateSymbolicLink(temp.GetPath(), escape));

  EXPECT_FALSE(InitializeLinuxMailHelperSandbox(mail_root));
  EXPECT_FALSE(IsLinuxMailHelperSandboxEngagedForTesting());
}

TEST(MahoMailHelperSandboxLinuxTest, HardLinkInsideMailRootFailsClosed) {
  base::ScopedTempDir temp;
  const base::FilePath mail_root = CreateMailRoot(&temp);
  const base::FilePath outside = temp.GetPath().AppendASCII("outside");
  const base::FilePath linked = mail_root.AppendASCII("linked");
  ASSERT_TRUE(base::WriteFile(outside, "outside"));
  ASSERT_EQ(0, link(outside.value().c_str(), linked.value().c_str()));

  EXPECT_FALSE(InitializeLinuxMailHelperSandbox(mail_root));
  EXPECT_FALSE(IsLinuxMailHelperSandboxEngagedForTesting());
}

}  // namespace
}  // namespace maho::mail_helper
