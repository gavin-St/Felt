#include "felt/bot_api.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

static_assert(sizeof(FeltCard) == 1);
static_assert(sizeof(FeltChips) == 8);
static_assert(sizeof(FeltAction) == 16);
static_assert(alignof(FeltAction) == 8);
static_assert(sizeof(FeltActionEvent) == 24);
static_assert(alignof(FeltActionEvent) == 8);
static_assert(sizeof(FeltGameState) == 128);
static_assert(alignof(FeltGameState) == 8);

static_assert(std::is_standard_layout_v<FeltAction>);
static_assert(std::is_trivially_copyable_v<FeltAction>);
static_assert(std::is_standard_layout_v<FeltActionEvent>);
static_assert(std::is_trivially_copyable_v<FeltActionEvent>);
static_assert(std::is_standard_layout_v<FeltGameState>);
static_assert(std::is_trivially_copyable_v<FeltGameState>);

static_assert(offsetof(FeltGameState, street) == 16);
static_assert(offsetof(FeltGameState, pot) == 32);
static_assert(offsetof(FeltGameState, decision_cap_us) == 96);
static_assert(offsetof(FeltGameState, decision_random) == 104);
static_assert(offsetof(FeltGameState, history) == 112);
static_assert(offsetof(FeltGameState, history_count) == 120);

static_assert(FELT_BOT_ABI_VERSION == UINT32_C(1));
static_assert(FELT_INVALID_CARD == UINT8_C(255));

int main() { return 0; }
