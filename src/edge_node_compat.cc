#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <mutex>

#if !defined(_WIN32)

#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include <uv.h>

#include "edge_node_addon_compat.h"

namespace node {

namespace {

constexpr unsigned kMaxSignal = 32;
constexpr int kStdioCount = 1 + STDERR_FILENO;

using SignalHandler =
    void (*)(int signal, siginfo_t* info, void* ucontext);
std::atomic<SignalHandler> g_signal_handlers[kMaxSignal];
std::atomic<bool> g_reset_signal_handlers[kMaxSignal];

struct StdioSnapshot {
  int flags = 0;
  bool valid = false;
  bool isatty = false;
  struct stat stat {};
  struct termios termios {};
};

std::once_flag g_stdio_init_once;
StdioSnapshot g_stdio[kStdioCount];

void ResetStdioImpl() {
  uv_tty_reset_mode();

  for (int fd = 0; fd < kStdioCount; ++fd) {
    StdioSnapshot& snapshot = g_stdio[fd];
    if (!snapshot.valid) continue;

    struct stat current_stat;
    if (fstat(fd, &current_stat) != 0) continue;
    if (snapshot.stat.st_dev != current_stat.st_dev ||
        snapshot.stat.st_ino != current_stat.st_ino) {
      continue;
    }

    int flags;
    do {
      flags = fcntl(fd, F_GETFL);
    } while (flags == -1 && errno == EINTR);
    if (flags != -1 && ((flags ^ snapshot.flags) & O_NONBLOCK) != 0) {
      flags &= ~O_NONBLOCK;
      flags |= snapshot.flags & O_NONBLOCK;
      int err;
      do {
        err = fcntl(fd, F_SETFL, flags);
      } while (err == -1 && errno == EINTR);
    }

    if (!snapshot.isatty) continue;

    sigset_t sigmask;
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGTTOU);
    (void)pthread_sigmask(SIG_BLOCK, &sigmask, nullptr);
    int err;
    do {
      err = tcsetattr(fd, TCSANOW, &snapshot.termios);
    } while (err == -1 && errno == EINTR);
    (void)pthread_sigmask(SIG_UNBLOCK, &sigmask, nullptr);
  }
}

void DispatchSignal(int signal) {
  if (signal <= 0 || static_cast<unsigned>(signal) >= kMaxSignal) return;
  const bool reset =
      g_reset_signal_handlers[signal].load(std::memory_order_relaxed);
  SignalHandler handler = reset
                              ? g_signal_handlers[signal].exchange(
                                    nullptr, std::memory_order_relaxed)
                              : g_signal_handlers[signal].load(
                                    std::memory_order_relaxed);
  if (reset) {
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    (void)sigaction(signal, &sa, nullptr);
  }
  if (handler != nullptr) handler(signal, nullptr, nullptr);
}

}  // namespace

__attribute__((visibility("default"))) void RegisterSignalHandler(
    int signal,
    SignalHandler handler,
    bool reset_handler) {
  if (handler == nullptr || signal <= 0 ||
      static_cast<unsigned>(signal) >= kMaxSignal) {
    return;
  }

  g_signal_handlers[signal].store(handler, std::memory_order_relaxed);
  g_reset_signal_handlers[signal].store(reset_handler,
                                        std::memory_order_relaxed);

  struct sigaction sa;
  std::memset(&sa, 0, sizeof(sa));
  // The registered Node callback shape includes siginfo and ucontext, but the
  // callbacks Edge installs do not consume either value. Register a correctly
  // typed POSIX handler and forward explicitly instead of depending on an ABI
  // to tolerate a mismatched indirect function call. The trampoline also owns
  // one-shot reset behavior so it does not depend on SA_RESETHAND support.
  sa.sa_handler = DispatchSignal;
  sa.sa_flags = 0;
  sigfillset(&sa.sa_mask);
  (void)sigaction(signal, &sa, nullptr);
}

__attribute__((visibility("default"))) void SignalExit(
    int signal,
    siginfo_t* /*info*/,
    void* /*ucontext*/) {
  ResetStdioImpl();
  raise(signal);
}

__attribute__((visibility("default"))) void InitializeStdio() {
  std::call_once(g_stdio_init_once, []() {
    std::atexit(ResetStdioImpl);
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    for (int fd = 0; fd < kStdioCount; ++fd) {
      StdioSnapshot& snapshot = g_stdio[fd];
      if (fstat(fd, &snapshot.stat) != 0) continue;

      snapshot.flags = fcntl(fd, F_GETFL);
      if (snapshot.flags == -1) continue;
      snapshot.valid = true;

      if (uv_guess_handle(fd) != UV_TTY) continue;
      snapshot.isatty = true;
      if (tcgetattr(fd, &snapshot.termios) != 0) {
        snapshot.isatty = false;
      }
    }
  });
}

__attribute__((visibility("default"))) void ResetStdio() {
  ResetStdioImpl();
}

__attribute__((visibility("default"))) void ResetSignalHandlers() {
  struct sigaction act;
  std::memset(&act, 0, sizeof(act));

  for (unsigned nr = 1; nr < kMaxSignal; nr += 1) {
    if (nr == SIGKILL || nr == SIGSTOP) continue;

    bool ignore_signal = false;
#if defined(SIGPIPE)
    ignore_signal = ignore_signal || nr == SIGPIPE;
#endif
#if defined(SIGXFSZ)
    ignore_signal = ignore_signal || nr == SIGXFSZ;
#endif
    act.sa_handler = ignore_signal ? SIG_IGN : SIG_DFL;

    if (act.sa_handler == SIG_DFL) {
      struct sigaction old;
      if (sigaction(static_cast<int>(nr), nullptr, &old) != 0) continue;
#if defined(SA_SIGINFO)
      if ((old.sa_flags & SA_SIGINFO) || old.sa_handler != SIG_IGN) continue;
#else
      if (old.sa_handler != SIG_IGN) continue;
#endif
    }

    (void)sigaction(static_cast<int>(nr), &act, nullptr);
  }
}

}  // namespace node

extern "C" __attribute__((visibility("default"))) void node_module_register(void* /*mod*/) {}

#endif  // !defined(_WIN32)
