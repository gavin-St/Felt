#include "felt/native_bot_runner.hpp"

#include <dlfcn.h>

#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace felt {
namespace {

constexpr std::size_t kMaxBotNameLength = 127;

template <typename Function>
Function load_function(void* handle, const std::string& path, const char* symbol) {
  static_assert(sizeof(Function) == sizeof(void*));

  (void)::dlerror();
  void* address = ::dlsym(handle, symbol);
  const char* error = ::dlerror();
  if (error != nullptr) {
    throw std::runtime_error("cannot load symbol '" + std::string(symbol) +
                             "' from '" + path + "': " + error);
  }

  Function function = nullptr;
  std::memcpy(&function, &address, sizeof(function));
  return function;
}

}  // namespace

NativeBotRunner::NativeBotRunner(std::string path) : path_(std::move(path)) {
  handle_ = ::dlopen(path_.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (handle_ == nullptr) {
    const char* error = ::dlerror();
    throw std::runtime_error("cannot load bot '" + path_ + "': " +
                             (error == nullptr ? "unknown dlopen error" : error));
  }

  try {
    const FeltBotAbiVersionFn abi_version =
        load_function<FeltBotAbiVersionFn>(handle_, path_, "felt_bot_abi_version");
    const FeltBotNameFn bot_name =
        load_function<FeltBotNameFn>(handle_, path_, "felt_bot_name");
    act_ = load_function<FeltBotActFn>(handle_, path_, "felt_bot_act");

    const uint32_t actual_version = abi_version();
    if (actual_version != FELT_BOT_ABI_VERSION) {
      throw std::runtime_error("bot '" + path_ + "' has ABI version " +
                               std::to_string(actual_version) + ", expected " +
                               std::to_string(FELT_BOT_ABI_VERSION));
    }

    const char* raw_name = bot_name();
    if (raw_name == nullptr) {
      throw std::runtime_error("bot '" + path_ + "' returned a null name");
    }

    const std::size_t name_length = ::strnlen(raw_name, kMaxBotNameLength + 1);
    if (name_length == 0 || name_length > kMaxBotNameLength) {
      throw std::runtime_error("bot '" + path_ +
                               "' name must contain 1 to 127 bytes");
    }
    name_.assign(raw_name, name_length);
  } catch (...) {
    (void)::dlclose(handle_);
    handle_ = nullptr;
    throw;
  }
}

NativeBotRunner::~NativeBotRunner() {
  if (handle_ != nullptr) {
    (void)::dlclose(handle_);
  }
}

std::string_view NativeBotRunner::name() const noexcept { return name_; }

FeltAction NativeBotRunner::act(const FeltGameState& state) { return act_(&state); }

const std::string& NativeBotRunner::path() const noexcept { return path_; }

}  // namespace felt
