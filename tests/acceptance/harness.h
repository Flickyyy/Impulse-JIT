#pragma once

#include <string>
#include <vector>

namespace impulse::tests::acceptance {

struct CaseReport {
    std::string name;
    bool success = false;
    std::vector<std::string> messages;
};

[[nodiscard]] auto run_suite() -> std::vector<CaseReport>;

}  // namespace impulse::tests::acceptance
