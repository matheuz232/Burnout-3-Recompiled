#pragma once

#include "analysis/r5900_reachability.h"

#include <string>

namespace b3r::analysis {

[[nodiscard]] std::string
render_r5900_analysis_report(const R5900ReachabilityGraph& graph);

} // namespace b3r::analysis
