#include <cstdlib>
#include <iostream>

#include "acceptance/harness.h"

using impulse::tests::acceptance::CaseReport;

auto runAcceptanceTests() -> int {
    const auto reports = impulse::tests::acceptance::run_suite();
    int passed = 0;
    for (const CaseReport& report : reports) {
        if (report.success) {
            ++passed;
            continue;
        }
        std::cerr << "Acceptance case '" << report.name << "' failed:\n";
        for (const auto& message : report.messages) {
            std::cerr << "  - " << message << '\n';
        }
        std::abort();
    }
    return passed;
}
