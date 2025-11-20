#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "impulse/frontend/lowering.h"
#include "impulse/frontend/parser.h"
#include "impulse/frontend/semantic.h"
#include "impulse/ir/interpreter.h"
#include "impulse/runtime/runtime.h"

namespace {

struct Options {
    std::string filePath;
    bool emitIR = false;
    bool check = false;
    bool evaluate = false;
    bool run = false;
    std::optional<std::string> evalBinding;
    std::optional<std::string> entryBinding;
};

void printUsage() {
    std::cout << "Usage: impulse-cpp --file <path> [--emit-ir] [--check] [--evaluate] [--eval-binding <name>] [--run] [--entry-binding <name>]\n";
}

auto parseArgs(int argc, char** argv) -> std::optional<Options> {
    Options opts;
    for (int i = 1; i < argc; ++i) {
        const std::string arg{argv[i]};
        if (arg == "--file" || arg == "-f") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --file\n";
                return std::nullopt;
            }
            opts.filePath = argv[++i];
            continue;
        }
        if (arg == "--emit-ir") {
            opts.emitIR = true;
            continue;
        }
        if (arg == "--check") {
            opts.check = true;
            continue;
        }
        if (arg == "--evaluate") {
            opts.evaluate = true;
            continue;
        }
        if (arg == "--eval-binding") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --eval-binding\n";
                return std::nullopt;
            }
            opts.evalBinding = argv[++i];
            opts.evaluate = true;
            continue;
        }
        if (arg == "--run") {
            opts.run = true;
            continue;
        }
        if (arg == "--entry-binding") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --entry-binding\n";
                return std::nullopt;
            }
            opts.entryBinding = argv[++i];
            opts.run = true;
            continue;
        }
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return std::nullopt;
        }
        std::cerr << "Unknown argument: " << arg << '\n';
        return std::nullopt;
    }

    if (opts.filePath.empty()) {
        std::cerr << "--file is required\n";
        return std::nullopt;
    }

    return opts;
}

auto readFile(const std::string& path) -> std::optional<std::string> {
    std::ifstream stream(path);
    if (!stream) {
        return std::nullopt;
    }
    std::string contents((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return contents;
}

void printDiagnostics(const std::vector<impulse::frontend::Diagnostic>& diagnostics) {
    for (const auto& diag : diagnostics) {
        std::cerr << '[' << diag.location.line << ':' << diag.location.column << "] " << diag.message << '\n';
    }
}

void printBindingResult(const std::string& name, const impulse::ir::BindingEvalResult& result) {
    using impulse::ir::EvalStatus;
    if (result.status == EvalStatus::Success && result.value.has_value()) {
        std::cout << name << " = " << *result.value << '\n';
        return;
    }
    std::string message = "not constant";
    if (!result.message.empty()) {
        message = result.message;
    } else if (result.status == EvalStatus::NonConstant) {
        message = "depends on unevaluated bindings";
    }
    std::cout << name << " = <unevaluated> (" << message << ")\n";
}

auto joinModulePath(const std::vector<std::string>& path) -> std::string {
    if (path.empty()) {
        return "<anonymous>";
    }
    std::ostringstream builder;
    for (size_t i = 0; i < path.size(); ++i) {
        if (i != 0) {
            builder << "::";
        }
        builder << path[i];
    }
    return builder.str();
}

}  // namespace

auto main(int argc, char** argv) -> int {
    const auto options = parseArgs(argc, argv);
    if (!options.has_value()) {
        return 1;
    }

    const auto source = readFile(options->filePath);
    if (!source.has_value()) {
        std::cerr << "Failed to read file: " << options->filePath << '\n';
        return 1;
    }

    impulse::frontend::Parser parser(*source);
    auto parseResult = parser.parseModule();
    if (!parseResult.success) {
        std::cerr << "Parse failed with " << parseResult.diagnostics.size() << " diagnostics:\n";
        printDiagnostics(parseResult.diagnostics);
        return 2;
    }

    if (options->emitIR) {
        const auto irText = impulse::frontend::emit_ir_text(parseResult.module);
        std::cout << irText;
        return 0;
    }

    auto runSemantic = [&]() -> std::optional<impulse::frontend::SemanticResult> {
        auto semantic = impulse::frontend::analyzeModule(parseResult.module);
        if (!semantic.success) {
            std::cerr << "Semantic check failed with " << semantic.diagnostics.size() << " diagnostics:\n";
            printDiagnostics(semantic.diagnostics);
            return std::nullopt;
        }
        return semantic;
    };

    std::optional<impulse::ir::Module> loweredModule;
    auto ensureLoweredModule = [&]() -> const impulse::ir::Module& {
        if (!loweredModule.has_value()) {
            loweredModule = impulse::frontend::lower_to_ir(parseResult.module);
        }
        return *loweredModule;
    };

    std::unordered_map<std::string, double> environment;
    std::vector<std::pair<std::string, impulse::ir::BindingEvalResult>> evaluations;
    auto interpretAllBindings = [&]() -> bool {
        const auto& lowered = ensureLoweredModule();
        environment.clear();
        evaluations.clear();
        evaluations.reserve(lowered.bindings.size());
        bool evalSuccess = true;

        for (const auto& binding : lowered.bindings) {
            auto eval = impulse::ir::interpret_binding(binding, environment);
            if (eval.status == impulse::ir::EvalStatus::Success && eval.value.has_value()) {
                environment.emplace(binding.name, *eval.value);
            } else if (eval.status == impulse::ir::EvalStatus::Error) {
                evalSuccess = false;
            }
            evaluations.emplace_back(binding.name, std::move(eval));
        }
        return evalSuccess;
    };

    if (options->check) {
        const auto semantic = runSemantic();
        if (!semantic.has_value()) {
            return 2;
        }
        std::cout << "Semantic checks passed\n";
        return 0;
    }

    if (options->run || options->entryBinding.has_value()) {
        const auto semantic = runSemantic();
        if (!semantic.has_value()) {
            return 2;
        }

        const bool evalSuccess = interpretAllBindings();
        const std::string entry = options->entryBinding.value_or("main");

        bool triedRuntime = false;
        if (loweredModule.has_value()) {
            impulse::runtime::Vm vm;
            const auto loadResult = vm.load(*loweredModule);
            if (!loadResult.success) {
                for (const auto& diag : loadResult.diagnostics) {
                    std::cerr << "runtime load error: " << diag << '\n';
                }
            } else {
                triedRuntime = true;
                const auto moduleName = joinModulePath(loweredModule->path);
                const auto vmResult = vm.run(moduleName, entry);
                if (vmResult.status == impulse::runtime::VmStatus::Success && vmResult.has_value) {
                    const int exitCode = static_cast<int>(std::llround(vmResult.value));
                    std::cout << "Program exited with " << exitCode << '\n';
                    return evalSuccess ? 0 : 2;
                }
                if (vmResult.status != impulse::runtime::VmStatus::MissingSymbol) {
                    std::string reason = vmResult.message;
                    if (reason.empty()) {
                        reason = "runtime execution failed";
                    }
                    std::cerr << "Entry function '" << entry << "' failed: " << reason << '\n';
                    return 2;
                }
            }
        }

        const auto it = std::find_if(evaluations.begin(), evaluations.end(), [&](const auto& pair) {
            return pair.first == entry;
        });

        if (it == evaluations.end()) {
            if (triedRuntime) {
                std::cerr << "Symbol '" << entry << "' not found\n";
            } else {
                std::cerr << "Binding '" << entry << "' not found\n";
            }
            return 3;
        }

        if (it->second.status == impulse::ir::EvalStatus::Success && it->second.value.has_value()) {
            const int exitCode = static_cast<int>(std::llround(*it->second.value));
            std::cout << "Program exited with " << exitCode << '\n';
            return evalSuccess ? 0 : 2;
        }

        std::string reason = it->second.message;
        if (reason.empty()) {
            reason = (it->second.status == impulse::ir::EvalStatus::NonConstant) ? "binding is not constant"
                                                                                : "evaluation error";
        }
        std::cerr << "Entry binding '" << entry << "' failed: " << reason << '\n';
        return 2;
    }

    if (options->evaluate || options->evalBinding.has_value()) {
        const auto semantic = runSemantic();
        if (!semantic.has_value()) {
            return 2;
        }

        const bool evalSuccess = interpretAllBindings();

        if (options->evalBinding.has_value()) {
            const auto& name = *options->evalBinding;
            const auto it = std::find_if(evaluations.begin(), evaluations.end(), [&](const auto& pair) {
                return pair.first == name;
            });
            if (it == evaluations.end()) {
                std::cerr << "Binding '" << name << "' not found\n";
                return 3;
            }
            printBindingResult(it->first, it->second);
        } else {
            for (const auto& [name, result] : evaluations) {
                printBindingResult(name, result);
            }
        }

        if (!evalSuccess) {
            return 2;
        }
        return 0;
    }

    std::cout << "Parse successful\n";
    return 0;
}
