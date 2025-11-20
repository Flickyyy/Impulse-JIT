#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "impulse/ir/ir.h"

namespace impulse::runtime {

enum class VmStatus : std::uint8_t {
    Success,
    ModuleError,
    MissingSymbol,
    RuntimeError,
};

struct VmResult {
    VmStatus status = VmStatus::RuntimeError;
    bool has_value = false;
    double value = 0.0;
    std::string message;
};

struct VmLoadResult {
    bool success = true;
    std::vector<std::string> diagnostics;
};

class Vm {
public:
    auto load(ir::Module module) -> VmLoadResult;

    [[nodiscard]] auto run(const std::string& module_name, const std::string& entry) const -> VmResult;

private:
    struct LoadedModule {
        std::string name;
        ir::Module module;
        std::unordered_map<std::string, double> globals;
    };

    [[nodiscard]] auto execute_function(const LoadedModule& module, const ir::Function& function,
                                        const std::unordered_map<std::string, double>& parameters) const -> VmResult;

    [[nodiscard]] static auto normalize_module_name(const ir::Module& module) -> std::string;

    std::vector<LoadedModule> modules_;
};

}  // namespace impulse::runtime
