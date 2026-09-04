#include "felt/match_process.hpp"

#include "felt/match.hpp"
#include "felt/match_log.hpp"
#include "felt/native_bot_runner.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <limits>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

namespace felt {
namespace {

enum class MessageType : std::uint32_t {
  decision_started = 1,
  decision_finished = 2,
  hand_finished = 3,
};

struct SupervisorMessage {
  MessageType type{};
  std::uint32_t bot_index{};
  std::uint32_t position{};
  std::uint32_t street{};
  std::uint64_t hand_index{};
  std::uint64_t decision_index{};
  std::uint64_t monotonic_ns{};
};

static_assert(sizeof(SupervisorMessage) <= PIPE_BUF);

std::uint64_t monotonic_ns() {
  timespec value{};
  if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
    throw std::runtime_error("could not read monotonic clock");
  }
  return static_cast<std::uint64_t>(value.tv_sec) * 1'000'000'000U +
         static_cast<std::uint64_t>(value.tv_nsec);
}

void send_message(int descriptor, const SupervisorMessage& message) {
  const char* data = reinterpret_cast<const char*>(&message);
  std::size_t remaining = sizeof(message);
  while (remaining != 0U) {
    const ssize_t written = ::write(descriptor, data, remaining);
    if (written < 0 && errno == EINTR) {
      continue;
    }
    if (written <= 0) {
      throw std::runtime_error("could not report worker progress");
    }
    data += written;
    remaining -= static_cast<std::size_t>(written);
  }
}

struct WorkerContext {
  int descriptor{-1};
  std::uint64_t hand_index{};
  std::uint64_t decision_index{};
};

class ReportingBotRunner final : public BotRunner {
 public:
  ReportingBotRunner(BotRunner& inner,
                     std::uint32_t bot_index,
                     WorkerContext& context)
      : inner_(inner), bot_index_(bot_index), context_(context) {}

  std::string_view name() const noexcept override { return inner_.name(); }

  FeltAction act(const FeltGameState& state) override {
    const SupervisorMessage started{
        MessageType::decision_started,
        bot_index_,
        state.position,
        state.street,
        context_.hand_index,
        context_.decision_index,
        monotonic_ns(),
    };
    send_message(context_.descriptor, started);
    const FeltAction action = inner_.act(state);
    send_message(context_.descriptor,
                 SupervisorMessage{MessageType::decision_finished,
                                   bot_index_,
                                   state.position,
                                   state.street,
                                   context_.hand_index,
                                   context_.decision_index,
                                   monotonic_ns()});
    ++context_.decision_index;
    return action;
  }

 private:
  BotRunner& inner_;
  std::uint32_t bot_index_;
  WorkerContext& context_;
};

class ReportingObserver final : public MatchObserver {
 public:
  ReportingObserver(MatchLogWriter& log, WorkerContext& context)
      : log_(log), context_(context) {}

  void on_hand(const MatchHand& hand) override {
    log_.on_hand(hand);
    log_.flush();
    context_.hand_index = hand.hand_index + 1U;
    send_message(context_.descriptor,
                 SupervisorMessage{MessageType::hand_finished,
                                   0,
                                   0,
                                   0,
                                   hand.hand_index,
                                   context_.decision_index,
                                   monotonic_ns()});
  }

 private:
  MatchLogWriter& log_;
  WorkerContext& context_;
};

void print_bot_result(std::string_view name,
                      std::size_t bot_index,
                      const MatchResult& result) {
  std::cout << name << ": adjusted_net_chips="
            << result.adjusted_net_by_bot[bot_index]
            << " raw_net_chips=" << result.raw_net_by_bot[bot_index]
            << " adjusted_button="
            << result.adjusted_net_by_bot_and_position[bot_index]
                                                        [FELT_POSITION_BUTTON]
            << " adjusted_big_blind="
            << result.adjusted_net_by_bot_and_position[bot_index]
                                                        [FELT_POSITION_BIG_BLIND]
            << '\n';
}

int run_worker(const MatchCliOptions& options, int descriptor) {
  try {
    NativeBotRunner native_a(options.bot_paths[0]);
    NativeBotRunner native_b(options.bot_paths[1]);
    std::array<BotArtifact, 2> artifacts{
        inspect_bot_artifact(options.bot_paths[0], std::string(native_a.name())),
        inspect_bot_artifact(options.bot_paths[1], std::string(native_b.name()))};
    MatchLogWriter log(options.output_directory, options.match,
                       std::move(artifacts));
    WorkerContext context{descriptor};
    ReportingBotRunner bot_a(native_a, 0, context);
    ReportingBotRunner bot_b(native_b, 1, context);
    ReportingObserver observer(log, context);
    const MatchResult result =
        play_match(options.match, bot_a, bot_b, &observer);
    log.finish(result);

    std::cout << "Felt match complete\n"
              << "seed=" << options.match.match_seed << '\n'
              << "hands=" << result.hand_count
              << " duplicate=" << (options.match.duplicate ? "true" : "false")
              << " equity_adjustment="
              << (options.match.equity_adjustment ? "true" : "false") << '\n'
              << "output=" << options.output_directory << '\n';
    print_bot_result(bot_a.name(), 0, result);
    print_bot_result(bot_b.name(), 1, result);
    std::cout << "adjusted payouts are the headline result; raw runout payouts "
                 "are retained for inspection\n";
  } catch (const std::exception& error) {
    std::cerr << "run_match worker: " << error.what() << '\n';
    return 1;
  }
  return 0;
}

pid_t wait_for_worker(pid_t worker, int& status, int options) {
  for (;;) {
    const pid_t result = ::waitpid(worker, &status, options);
    if (result < 0 && errno == EINTR) {
      continue;
    }
    return result;
  }
}

int poll_timeout_ms(bool active,
                    std::uint64_t deadline_ns,
                    std::uint64_t now_ns) {
  if (!active) {
    return -1;
  }
  if (now_ns >= deadline_ns) {
    return 0;
  }
  const std::uint64_t remaining_ns = deadline_ns - now_ns;
  const std::uint64_t milliseconds =
      remaining_ns / 1'000'000U + (remaining_ns % 1'000'000U != 0U);
  return static_cast<int>(std::min<std::uint64_t>(milliseconds, INT_MAX));
}

void kill_and_reap(pid_t worker, int& status) {
  if (::kill(worker, SIGKILL) != 0 && errno != ESRCH) {
    throw std::runtime_error("could not kill unresponsive match worker");
  }
  if (wait_for_worker(worker, status, 0) != worker && errno != ECHILD) {
    throw std::runtime_error("could not reap match worker");
  }
}

void mark_aborted_if_started(const MatchCliOptions& options,
                             const MatchAbortInfo& abort) {
  const std::filesystem::path summary =
      std::filesystem::path(options.output_directory) / "summary.json";
  if (!std::filesystem::is_regular_file(summary)) {
    return;
  }
  try {
    mark_match_log_aborted(options.output_directory, abort);
  } catch (const std::exception& error) {
    std::cerr << "run_match: could not mark summary aborted: " << error.what()
              << '\n';
  }
}

}  // namespace

int run_supervised_match(const MatchCliOptions& options) {
  int descriptors[2]{};
  if (::pipe(descriptors) != 0) {
    throw std::runtime_error("could not create supervisor pipe");
  }

  const pid_t worker = ::fork();
  if (worker < 0) {
    const int saved_errno = errno;
    (void)::close(descriptors[0]);
    (void)::close(descriptors[1]);
    throw std::runtime_error(std::string("could not create match worker: ") +
                             std::strerror(saved_errno));
  }
  if (worker == 0) {
    (void)::close(descriptors[0]);
    const int result = run_worker(options, descriptors[1]);
    (void)::close(descriptors[1]);
    std::cout.flush();
    std::cerr.flush();
    std::_Exit(result);
  }

  (void)::close(descriptors[1]);
  const int flags = ::fcntl(descriptors[0], F_GETFL, 0);
  if (flags < 0 || ::fcntl(descriptors[0], F_SETFL, flags | O_NONBLOCK) != 0) {
    int status = 0;
    kill_and_reap(worker, status);
    (void)::close(descriptors[0]);
    throw std::runtime_error("could not configure supervisor pipe");
  }

  MatchAbortInfo abort;
  abort.hard_timeout_ms = options.hard_timeout_ms;
  bool active = false;
  bool pipe_closed = false;
  bool protocol_error = false;
  std::uint64_t deadline_ns = 0;
  int status = 0;

  for (;;) {
    pollfd descriptor{descriptors[0], POLLIN, 0};
    const int timeout = poll_timeout_ms(active, deadline_ns, monotonic_ns());
    int polled = 0;
    do {
      polled = ::poll(&descriptor, 1, timeout);
    } while (polled < 0 && errno == EINTR);
    if (polled < 0) {
      protocol_error = true;
    }
    if (polled > 0 && (descriptor.revents & (POLLERR | POLLNVAL)) != 0) {
      protocol_error = true;
    }

    if (polled > 0 && (descriptor.revents & (POLLIN | POLLHUP)) != 0) {
      for (;;) {
        SupervisorMessage message;
        const ssize_t bytes = ::read(descriptors[0], &message, sizeof(message));
        if (bytes == static_cast<ssize_t>(sizeof(message))) {
          if (message.type == MessageType::decision_started) {
            if (active) {
              protocol_error = true;
              break;
            }
            active = true;
            abort.has_active_decision = true;
            abort.hand_index = message.hand_index;
            abort.decision_index = message.decision_index;
            abort.bot_index = message.bot_index;
            abort.position = message.position;
            abort.street = message.street;
            const std::uint64_t timeout_ns =
                options.hard_timeout_ms * 1'000'000U;
            deadline_ns =
                message.monotonic_ns >
                        std::numeric_limits<std::uint64_t>::max() - timeout_ns
                    ? std::numeric_limits<std::uint64_t>::max()
                    : message.monotonic_ns + timeout_ns;
          } else if (message.type == MessageType::decision_finished) {
            if (!active || message.decision_index != abort.decision_index) {
              protocol_error = true;
              break;
            }
            active = false;
            abort.has_active_decision = false;
          } else if (message.type == MessageType::hand_finished) {
            abort.completed_hands = message.hand_index + 1U;
          } else {
            protocol_error = true;
            break;
          }
          continue;
        }
        if (bytes == 0) {
          pipe_closed = true;
          break;
        }
        if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
          break;
        }
        if (bytes < 0 && errno == EINTR) {
          continue;
        }
        protocol_error = true;
        break;
      }
    }

    const pid_t waited = wait_for_worker(worker, status, WNOHANG);
    if (waited == worker) {
      break;
    }
    if (waited < 0) {
      protocol_error = true;
    }
    if (pipe_closed && !protocol_error) {
      if (wait_for_worker(worker, status, 0) != worker) {
        protocol_error = true;
      } else {
        break;
      }
    }
    if (protocol_error) {
      abort.reason = "supervisor_protocol_error";
      kill_and_reap(worker, status);
      abort.worker_status = status;
      mark_aborted_if_started(options, abort);
      (void)::close(descriptors[0]);
      std::cerr << "run_match: aborted because worker supervision failed\n";
      return 1;
    }
    if (active && monotonic_ns() >= deadline_ns) {
      abort.reason = "decision_wall_timeout";
      kill_and_reap(worker, status);
      abort.worker_status = status;
      mark_aborted_if_started(options, abort);
      (void)::close(descriptors[0]);
      std::cerr << "run_match: aborted after bot " << abort.bot_index
                << " exceeded the " << abort.hard_timeout_ms
                << " ms hard timeout on hand " << abort.hand_index
                << ", decision " << abort.decision_index << '\n';
      return 124;
    }
  }

  (void)::close(descriptors[0]);
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    return 0;
  }

  abort.reason = WIFSIGNALED(status) ? "worker_signal" : "worker_error";
  abort.worker_status = status;
  mark_aborted_if_started(options, abort);
  if (WIFSIGNALED(status)) {
    const int signal = WTERMSIG(status);
    std::cerr << "run_match: worker terminated by signal " << signal << '\n';
    return 128 + signal;
  }
  const int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
  std::cerr << "run_match: worker exited with status " << exit_code << '\n';
  return exit_code == 0 ? 1 : exit_code;
}

}  // namespace felt
