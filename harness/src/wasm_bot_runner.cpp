#include "felt/wasm_bot_runner.hpp"

#include "felt/wasm_bot_api.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

#if FELT_HAS_WASMTIME
#include <wasmtime.hh>

#include <filesystem>
#include <fstream>
#include <vector>
#endif

namespace felt {

#if FELT_HAS_WASMTIME
namespace {

constexpr std::uintmax_t kMaxModuleBytes = 8U * 1024U * 1024U;
constexpr std::int64_t kMaxMemoryBytes = 16 * 1024 * 1024;
constexpr std::uint64_t kSetupFuel = 1'000'000U;
constexpr std::uint64_t kMinimumDecisionFuel = 100'000U;
constexpr std::uint64_t kFuelPerMicrosecond = 1'000U;
constexpr std::size_t kMaxBotNameLength = 127U;

using NoParams = std::tuple<>;
using NoResults = std::tuple<>;
using I32Function = wasmtime::TypedFunc<NoParams, std::int32_t>;
using VoidFunction = wasmtime::TypedFunc<NoParams, NoResults>;

wasmtime::Engine make_engine() {
  wasmtime::Config config;
  config.consume_fuel(true);
  config.max_wasm_stack(512U * 1024U);
  config.shared_memory(false);
  config.wasm_memory64(false);
  config.wasm_multi_memory(false);
  return wasmtime::Engine(std::move(config));
}

std::vector<std::uint8_t> read_module(const std::string& path) {
  std::error_code error;
  const std::uintmax_t size = std::filesystem::file_size(path, error);
  if (error) {
    throw std::runtime_error("cannot inspect Wasm bot '" + path + "': " +
                             error.message());
  }
  if (size == 0U || size > kMaxModuleBytes) {
    throw std::runtime_error("Wasm bot '" + path +
                             "' must contain 1 byte to 8 MiB");
  }

  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open Wasm bot '" + path + "'");
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  input.read(reinterpret_cast<char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  if (!input || input.gcount() != static_cast<std::streamsize>(bytes.size())) {
    throw std::runtime_error("cannot read Wasm bot '" + path + "'");
  }
  return bytes;
}

wasmtime::Module compile_module(wasmtime::Engine& engine,
                                const std::string& path) {
  std::vector<std::uint8_t> bytes = read_module(path);
  auto result = wasmtime::Module::compile(engine, bytes);
  if (!result) {
    throw std::runtime_error("cannot compile Wasm bot '" + path + "': " +
                             result.err().message());
  }
  wasmtime::Module module = result.ok();
  if (module.imports().size() != 0U) {
    throw std::runtime_error("Wasm bot '" + path +
                             "' imports host functions; Felt Wasm bots must "
                             "be self-contained");
  }
  return module;
}

wasmtime::Instance instantiate(wasmtime::Store& store,
                               const wasmtime::Module& module,
                               const std::string& path) {
  auto result = wasmtime::Instance::create(store, module, {});
  if (!result) {
    throw std::runtime_error("cannot instantiate Wasm bot '" + path + "': " +
                             result.err().message());
  }
  return result.ok();
}

template <typename External>
External require_export(wasmtime::Store& store,
                        wasmtime::Instance& instance,
                        const std::string& path,
                        std::string_view name) {
  auto value = instance.get(store, name);
  if (!value) {
    throw std::runtime_error("Wasm bot '" + path + "' is missing export '" +
                             std::string(name) + "'");
  }
  const External* external = std::get_if<External>(&*value);
  if (external == nullptr) {
    throw std::runtime_error("Wasm bot '" + path + "' export '" +
                             std::string(name) + "' has the wrong kind");
  }
  return *external;
}

template <typename Results>
wasmtime::TypedFunc<NoParams, Results> require_function(
    wasmtime::Store& store,
    wasmtime::Instance& instance,
    const std::string& path,
    std::string_view name) {
  wasmtime::Func function =
      require_export<wasmtime::Func>(store, instance, path, name);
  auto typed = function.typed<NoParams, Results>(store);
  if (!typed) {
    throw std::runtime_error("Wasm bot '" + path + "' export '" +
                             std::string(name) +
                             "' has the wrong function signature");
  }
  return typed.ok();
}

void set_fuel(wasmtime::Store& store,
              std::uint64_t fuel,
              const std::string& path) {
  auto result = wasmtime::Store::Context(store).set_fuel(fuel);
  if (!result) {
    throw std::runtime_error("cannot set fuel for Wasm bot '" + path +
                             "': " + result.err().message());
  }
}

template <typename Results>
Results call(wasmtime::Store& store,
             const wasmtime::TypedFunc<NoParams, Results>& function,
             const std::string& path,
             std::string_view export_name) {
  auto result = function.call(store, NoParams{});
  if (!result) {
    throw std::runtime_error("Wasm bot '" + path + "' trapped in '" +
                             std::string(export_name) + "': " +
                             result.err().message());
  }
  return result.ok();
}

std::uint32_t call_offset(wasmtime::Store& store,
                          const I32Function& function,
                          const std::string& path,
                          std::string_view export_name) {
  return static_cast<std::uint32_t>(
      call<std::int32_t>(store, function, path, export_name));
}

void require_range(wasmtime::Store& store,
                   const wasmtime::Memory& memory,
                   const std::string& path,
                   std::uint32_t offset,
                   std::size_t length,
                   std::string_view label) {
  const std::size_t memory_size = memory.data(store).size();
  if (static_cast<std::size_t>(offset) > memory_size ||
      length > memory_size - static_cast<std::size_t>(offset)) {
    throw std::runtime_error("Wasm bot '" + path + "' has an out-of-bounds " +
                             std::string(label) + " buffer");
  }
}

std::uint64_t decision_fuel(std::uint64_t cap_us) {
  if (cap_us >
      std::numeric_limits<std::uint64_t>::max() / kFuelPerMicrosecond) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  return std::max(kMinimumDecisionFuel, cap_us * kFuelPerMicrosecond);
}

}  // namespace

struct WasmBotRunner::Impl {
  explicit Impl(const std::string& path)
      : engine(make_engine()),
        store(engine),
        module(compile_module(engine, path)),
        instance(prepare_instance(path)),
        memory(require_export<wasmtime::Memory>(store, instance, path,
                                                "memory")),
        abi_version(require_function<std::int32_t>(
            store, instance, path, "felt_wasm_bot_abi_version")),
        state_ptr(require_function<std::int32_t>(
            store, instance, path, "felt_wasm_state_ptr")),
        history_ptr(require_function<std::int32_t>(
            store, instance, path, "felt_wasm_history_ptr")),
        history_capacity(require_function<std::int32_t>(
            store, instance, path, "felt_wasm_history_capacity")),
        action_ptr(require_function<std::int32_t>(
            store, instance, path, "felt_wasm_action_ptr")),
        name_ptr(require_function<std::int32_t>(
            store, instance, path, "felt_wasm_bot_name_ptr")),
        act_function(require_function<NoResults>(store, instance, path,
                                                 "felt_wasm_act")) {}

  wasmtime::Instance prepare_instance(const std::string& path) {
    store.limiter(kMaxMemoryBytes, 4096, 1, 1, 1);
    set_fuel(store, kSetupFuel, path);
    return instantiate(store, module, path);
  }

  wasmtime::Engine engine;
  wasmtime::Store store;
  wasmtime::Module module;
  wasmtime::Instance instance;
  wasmtime::Memory memory;
  I32Function abi_version;
  I32Function state_ptr;
  I32Function history_ptr;
  I32Function history_capacity;
  I32Function action_ptr;
  I32Function name_ptr;
  VoidFunction act_function;
  std::uint32_t state_offset{};
  std::uint32_t history_offset{};
  std::uint32_t action_offset{};
};

WasmBotRunner::WasmBotRunner(std::string path) : path_(std::move(path)) {
  impl_ = std::make_unique<Impl>(path_);

  if (impl_->instance.get(impl_->store, "_initialize")) {
    VoidFunction initialize = require_function<NoResults>(
        impl_->store, impl_->instance, path_, "_initialize");
    set_fuel(impl_->store, kSetupFuel, path_);
    (void)call<NoResults>(impl_->store, initialize, path_, "_initialize");
  }

  set_fuel(impl_->store, kSetupFuel, path_);
  const std::uint32_t version =
      call_offset(impl_->store, impl_->abi_version, path_,
                  "felt_wasm_bot_abi_version");
  if (version != FELT_WASM_BOT_ABI_VERSION) {
    throw std::runtime_error("Wasm bot '" + path_ + "' has ABI version " +
                             std::to_string(version) + ", expected " +
                             std::to_string(FELT_WASM_BOT_ABI_VERSION));
  }

  impl_->state_offset = call_offset(impl_->store, impl_->state_ptr, path_,
                                    "felt_wasm_state_ptr");
  impl_->history_offset = call_offset(impl_->store, impl_->history_ptr, path_,
                                      "felt_wasm_history_ptr");
  impl_->action_offset = call_offset(impl_->store, impl_->action_ptr, path_,
                                     "felt_wasm_action_ptr");
  const std::uint32_t capacity =
      call_offset(impl_->store, impl_->history_capacity, path_,
                  "felt_wasm_history_capacity");
  if (capacity < FELT_WASM_MAX_HISTORY_EVENTS) {
    throw std::runtime_error("Wasm bot '" + path_ +
                             "' history buffer is smaller than the Felt ABI "
                             "requires");
  }

  require_range(impl_->store, impl_->memory, path_, impl_->state_offset,
                sizeof(FeltWasmGameState), "state");
  require_range(impl_->store, impl_->memory, path_, impl_->history_offset,
                sizeof(FeltActionEvent) * FELT_WASM_MAX_HISTORY_EVENTS,
                "history");
  require_range(impl_->store, impl_->memory, path_, impl_->action_offset,
                sizeof(FeltAction), "action");

  const std::uint32_t name_offset = call_offset(
      impl_->store, impl_->name_ptr, path_, "felt_wasm_bot_name_ptr");
  const auto memory = impl_->memory.data(impl_->store);
  if (static_cast<std::size_t>(name_offset) >= memory.size()) {
    throw std::runtime_error("Wasm bot '" + path_ +
                             "' returned an out-of-bounds name");
  }
  std::size_t length = 0U;
  while (length <= kMaxBotNameLength &&
         static_cast<std::size_t>(name_offset) + length < memory.size() &&
         memory[static_cast<std::size_t>(name_offset) + length] != 0U) {
    ++length;
  }
  if (length == 0U || length > kMaxBotNameLength ||
      static_cast<std::size_t>(name_offset) + length >= memory.size()) {
    throw std::runtime_error("Wasm bot '" + path_ +
                             "' name must contain 1 to 127 bytes");
  }
  name_.assign(reinterpret_cast<const char*>(memory.data() + name_offset),
               length);
}

WasmBotRunner::~WasmBotRunner() = default;

std::string_view WasmBotRunner::name() const noexcept { return name_; }

FeltAction WasmBotRunner::act(const FeltGameState& state) {
  if (state.history_count > FELT_WASM_MAX_HISTORY_EVENTS) {
    throw std::runtime_error("Wasm bot '" + path_ +
                             "' cannot receive more than 1024 history events");
  }

  FeltWasmGameState wire{};
  wire.abi_version = state.abi_version;
  wire.struct_size = sizeof(wire);
  std::copy(std::begin(state.hole), std::end(state.hole),
            std::begin(wire.hole));
  std::copy(std::begin(state.board), std::end(state.board),
            std::begin(wire.board));
  wire.board_count = state.board_count;
  wire.street = state.street;
  wire.position = state.position;
  wire.legal_actions = state.legal_actions;
  wire.reserved0 = state.reserved0;
  wire.pot = state.pot;
  wire.my_stack = state.my_stack;
  wire.opp_stack = state.opp_stack;
  wire.my_street_contribution = state.my_street_contribution;
  wire.opp_street_contribution = state.opp_street_contribution;
  wire.to_call = state.to_call;
  wire.min_raise_to = state.min_raise_to;
  wire.max_raise_to = state.max_raise_to;
  wire.decision_cap_us = state.decision_cap_us;
  wire.decision_random = state.decision_random;
  wire.history_count = state.history_count;
  wire.reserved1 = state.reserved1;

  auto memory = impl_->memory.data(impl_->store);
  std::memcpy(memory.data() + impl_->state_offset, &wire, sizeof(wire));
  if (state.history_count != 0U) {
    if (state.history == nullptr) {
      throw std::runtime_error("cannot pass null history to Wasm bot '" + path_ +
                               "'");
    }
    std::memcpy(memory.data() + impl_->history_offset, state.history,
                sizeof(FeltActionEvent) * state.history_count);
  }

  set_fuel(impl_->store, decision_fuel(state.decision_cap_us), path_);
  (void)call<NoResults>(impl_->store, impl_->act_function, path_,
                        "felt_wasm_act");

  memory = impl_->memory.data(impl_->store);
  require_range(impl_->store, impl_->memory, path_, impl_->action_offset,
                sizeof(FeltAction), "action");
  FeltAction action{};
  std::memcpy(&action, memory.data() + impl_->action_offset, sizeof(action));
  return action;
}

const std::string& WasmBotRunner::path() const noexcept { return path_; }

bool wasm_bot_runtime_available() noexcept { return true; }

#else

struct WasmBotRunner::Impl {};

WasmBotRunner::WasmBotRunner(std::string path) : path_(std::move(path)) {
  throw std::runtime_error(
      "cannot load Wasm bot '" + path_ +
      "': this Felt build has no Wasmtime runtime; run "
      "scripts/bootstrap_wasm.py and reconfigure CMake");
}

WasmBotRunner::~WasmBotRunner() = default;
std::string_view WasmBotRunner::name() const noexcept { return name_; }
FeltAction WasmBotRunner::act(const FeltGameState&) {
  throw std::runtime_error("Wasm runtime is unavailable");
}
const std::string& WasmBotRunner::path() const noexcept { return path_; }
bool wasm_bot_runtime_available() noexcept { return false; }

#endif

}  // namespace felt
