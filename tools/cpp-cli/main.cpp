#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>

#include "impulse/frontend/dump.h"
#include "impulse/frontend/lowering.h"
#include "impulse/frontend/parser.h"
#include "impulse/frontend/semantic.h"
#include "impulse/ir/analysis.h"
#include "impulse/ir/dump.h"
#include "impulse/ir/interpreter.h"
#include "impulse/runtime/runtime.h"

namespace {

struct DumpOption {
    bool enabled = false;
    std::optional<std::string> path;
};

struct Options {
    std::string filePath;
    bool emitIR = false;
    bool check = false;
    bool evaluate = false;
    bool run = false;
    std::optional<std::string> evalBinding;
    std::optional<std::string> entryBinding;
    DumpOption dumpTokens;
    DumpOption dumpAst;
    DumpOption dumpIr;
    DumpOption dumpCfg;
    DumpOption dumpSsa;
    DumpOption dumpOptimisationLog;
    DumpOption traceRuntime;
    bool useProcessStdin = false;
    std::optional<std::string> stdinFile;
    std::optional<std::string> stdinText;
};

void printUsage() {
    std::cout << "Usage: impulse-cpp --file <path> [options]\n"
                 "\n"
                 "General options:\n"
                 "  --emit-ir                         Emit textual IR and exit\n"
                 "  --check                           Run semantic checks only\n"
                 "  --evaluate [--eval-binding <name>] Evaluate constant bindings\n"
                 "  --run [--entry-binding <name>]    Execute the program\n"
                 "\n"
                 "Introspection options (optional path argument writes to file):\n"
                 "  --dump-tokens [path]              Dump lexer tokens\n"
                 "  --dump-ast [path]                 Dump parsed AST\n"
                 "  --dump-ir [path]                  Dump lowered IR\n"
                 "  --dump-cfg [path]                 Dump CFG per function\n"
                 "  --dump-ssa [path]                 Dump SSA before/after optimisation\n"
                 "  --dump-optimisation-log [path]    Dump optimiser pass summary\n"
                 "  --trace-runtime [path]            Dump SSA execution trace during run\n"
                 "\n"
                 "Runtime input options:\n"
                 "  --stdin                           Read program input from process stdin\n"
                 "  --stdin-file <path>               Read program input from a file\n"
                 "  --stdin-text <value>              Provide inline program input\n";
}

auto parseOptionalOutputPath(int argc, char** argv, int& index) -> std::optional<std::string> {
    if (index + 1 >= argc) {
        return std::nullopt;
    }
    const std::string next_value{argv[index + 1]};
    if (!next_value.empty() && next_value.front() != '-') {
        ++index;
        return std::optional<std::string>{next_value};
    }
    return std::nullopt;
}

auto write_dump(const DumpOption& option, const std::string& label,
                const std::function<void(std::ostream&)>& writer) -> bool {
    if (!option.enabled) {
        return true;
    }

    if (option.path.has_value()) {
        std::ofstream file(*option.path);
        if (!file) {
            std::cerr << "Failed to open '" << *option.path << "' for " << label << " output\n";
            return false;
        }
        writer(file);
    } else {
        writer(std::cout);
    }
    return true;
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
        if (arg == "--dump-tokens") {
            opts.dumpTokens.enabled = true;
            opts.dumpTokens.path = parseOptionalOutputPath(argc, argv, i);
            continue;
        }
        if (arg == "--dump-ast") {
            opts.dumpAst.enabled = true;
            opts.dumpAst.path = parseOptionalOutputPath(argc, argv, i);
            continue;
        }
        if (arg == "--dump-ir") {
            opts.dumpIr.enabled = true;
            opts.dumpIr.path = parseOptionalOutputPath(argc, argv, i);
            continue;
        }
        if (arg == "--dump-cfg") {
            opts.dumpCfg.enabled = true;
            opts.dumpCfg.path = parseOptionalOutputPath(argc, argv, i);
            continue;
        }
        if (arg == "--dump-ssa") {
            opts.dumpSsa.enabled = true;
            opts.dumpSsa.path = parseOptionalOutputPath(argc, argv, i);
            continue;
        }
        if (arg == "--dump-optimisation-log") {
            opts.dumpOptimisationLog.enabled = true;
            opts.dumpOptimisationLog.path = parseOptionalOutputPath(argc, argv, i);
            continue;
        }
        if (arg == "--trace-runtime") {
            opts.traceRuntime.enabled = true;
            opts.traceRuntime.path = parseOptionalOutputPath(argc, argv, i);
            opts.run = true;
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
        if (arg == "--stdin") {
            opts.useProcessStdin = true;
            continue;
        }
        if (arg == "--stdin-file") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --stdin-file\n";
                return std::nullopt;
            }
            opts.stdinFile = argv[++i];
            continue;
        }
        if (arg == "--stdin-text") {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for --stdin-text\n";
                return std::nullopt;
            }
            opts.stdinText = argv[++i];
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

    const int stdinSources = static_cast<int>(opts.useProcessStdin) + (opts.stdinFile.has_value() ? 1 : 0) +
                             (opts.stdinText.has_value() ? 1 : 0);
    if (stdinSources > 1) {
        std::cerr << "Specify at most one of --stdin, --stdin-file, or --stdin-text\n";
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

    impulse::frontend::Lexer dumpLexer(*source);
    const auto tokens = dumpLexer.tokenize();

    if (!write_dump(options->dumpTokens, "token", [&](std::ostream& out) {
            impulse::frontend::dump_tokens(tokens, out);
        })) {
        return 1;
    }

    impulse::frontend::Parser parser(*source);
    auto parseResult = parser.parseModule();
    if (!parseResult.success) {
        std::cerr << "Parse failed with " << parseResult.diagnostics.size() << " diagnostics:\n";
        printDiagnostics(parseResult.diagnostics);
        return 2;
    }

    if (!write_dump(options->dumpAst, "AST", [&](std::ostream& out) {
            impulse::frontend::dump_ast(parseResult.module, out);
        })) {
        return 1;
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

    std::optional<std::vector<impulse::ir::FunctionAnalysis>> analysedFunctions;
    auto ensureAnalysedFunctions = [&]() -> const std::vector<impulse::ir::FunctionAnalysis>& {
        if (!analysedFunctions.has_value()) {
            analysedFunctions = impulse::ir::analyse_module(ensureLoweredModule());
        }
        return *analysedFunctions;
    };

    if (!write_dump(options->dumpIr, "IR", [&](std::ostream& out) {
            impulse::ir::dump_ir(ensureLoweredModule(), out);
        })) {
        return 1;
    }

    if (!write_dump(options->dumpCfg, "CFG", [&](std::ostream& out) {
            const auto& analyses = ensureAnalysedFunctions();
            for (const auto& entry : analyses) {
                out << "Function " << entry.name << '\n';
                impulse::ir::dump_cfg(entry.cfg, out);
                out << '\n';
            }
        })) {
        return 1;
    }

    if (!write_dump(options->dumpSsa, "SSA", [&](std::ostream& out) {
            const auto& analyses = ensureAnalysedFunctions();
            for (const auto& entry : analyses) {
                out << "Function " << entry.name << "\n== Before optimisation ==\n";
                impulse::ir::dump_ssa(entry.ssa_before, out, true);
                out << "== After optimisation ==\n";
                impulse::ir::dump_ssa(entry.ssa_after, out, true);
                out << '\n';
            }
        })) {
        return 1;
    }

    if (!write_dump(options->dumpOptimisationLog, "optimisation log", [&](std::ostream& out) {
            const auto& analyses = ensureAnalysedFunctions();
            for (const auto& entry : analyses) {
                out << "Function " << entry.name << '\n';
                for (const auto& line : entry.optimisation_log) {
                    out << "  " << line << '\n';
                }
                out << '\n';
            }
        })) {
        return 1;
    }

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

            std::ifstream stdinFileStream;
            std::istringstream stdinTextStream;
            if (options->stdinText.has_value()) {
                stdinTextStream.str(*options->stdinText);
                stdinTextStream.clear();
                vm.set_input_stream(&stdinTextStream);
            } else if (options->stdinFile.has_value()) {
                stdinFileStream.open(options->stdinFile->c_str(), std::ios::in);
                if (!stdinFileStream.is_open()) {
                    std::cerr << "Failed to open stdin file '" << *options->stdinFile << "'\n";
                    return 2;
                }
                vm.set_input_stream(&stdinFileStream);
            } else if (options->useProcessStdin) {
                vm.set_input_stream(&std::cin);
            }

            std::ofstream traceFile;
            std::ostringstream traceBuffer;
            std::ostream* traceStream = nullptr;
            bool traceToBuffer = false;

            if (options->traceRuntime.enabled) {
                if (options->traceRuntime.path.has_value()) {
                    traceFile.open(options->traceRuntime.path->c_str(), std::ios::out | std::ios::trunc);
                    if (!traceFile.is_open()) {
                        std::cerr << "Failed to open runtime trace output '" << *options->traceRuntime.path << "'\n";
                        return 2;
                    }
                    traceStream = &traceFile;
                } else {
                    traceStream = &traceBuffer;
                    traceToBuffer = true;
                }
            }

            const auto loadResult = vm.load(*loweredModule);
            if (!loadResult.success) {
                for (const auto& diag : loadResult.diagnostics) {
                    std::cerr << "runtime load error: " << diag << '\n';
                }
            } else {
                triedRuntime = true;
                if (traceStream != nullptr) {
                    vm.set_trace_stream(traceStream);
                }
                const auto moduleName = joinModulePath(loweredModule->path);
                const auto vmResult = vm.run(moduleName, entry);
                if (traceStream != nullptr) {
                    vm.set_trace_stream(nullptr);
                    if (traceToBuffer) {
                        const std::string output = traceBuffer.str();
                        if (!output.empty()) {
                            std::cout << output;
                            if (output.back() != '\n') {
                                std::cout << '\n';
                            }
                        }
                    } else {
                        traceFile.flush();
                    }
                }
                if (vmResult.status == impulse::runtime::VmStatus::Success && vmResult.has_value) {
                    // Print program output (from println/print calls)
                    if (!vmResult.message.empty()) {
                        std::cout << vmResult.message;
                        if (vmResult.message.back() != '\n') {
                            std::cout << '\n';
                        }
                    }
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
                    vm.set_input_stream(nullptr);
                    return 2;
                }
            }
            vm.set_input_stream(nullptr);
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
