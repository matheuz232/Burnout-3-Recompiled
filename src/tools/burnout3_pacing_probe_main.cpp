#include "tools/burnout3_pacing_probe_app.h"
#include "tools/burnout3_pacing_probe_options.h"

#include <cstddef>
#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char* argv[]) {
    std::vector<std::string_view> args{};
    if (argc > 1) {
        args.reserve(static_cast<std::size_t>(argc - 1));
    }
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    const auto parsed = b3r::tools::parse_burnout3_pacing_probe_options(args);
    if (!parsed.ok()) {
        std::cerr << "Burnout3PacingProbe: " << parsed.message << "\n\n"
                  << b3r::tools::burnout3_pacing_probe_usage();
        return 2;
    }

    if (parsed.options->show_help) {
        std::cout << b3r::tools::burnout3_pacing_probe_usage();
        return 0;
    }

    const auto result = b3r::tools::run_burnout3_pacing_probe(*parsed.options, std::cout);
    if (!result.ok()) {
        std::cerr << "Burnout3PacingProbe: " << result.message << '\n';
        return 3;
    }

    return 0;
}
