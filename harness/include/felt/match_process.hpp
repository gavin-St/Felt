#ifndef FELT_MATCH_PROCESS_HPP
#define FELT_MATCH_PROCESS_HPP

#include "felt/match_cli.hpp"

namespace felt {

// Runs one match in a supervised worker process. Returns a process-style exit
// code; 124 denotes a hard per-decision wall timeout.
[[nodiscard]] int run_supervised_match(const MatchCliOptions& options);

}  // namespace felt

#endif
