#include <gtest/gtest.h>
#include <sstream>

#include "acceptance/harness.h"

using impulse::tests::acceptance::CaseReport;

TEST(AcceptanceTest, AllCases) {
    const auto reports = impulse::tests::acceptance::run_suite();
    for (const CaseReport& report : reports) {
        if (!report.success) {
            std::ostringstream error_msg;
            error_msg << "Acceptance case '" << report.name << "' failed:\n";
            for (const auto& message : report.messages) {
                error_msg << "  - " << message << '\n';
            }
            FAIL() << error_msg.str();
        }
    }
}
