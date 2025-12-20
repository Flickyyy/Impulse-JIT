#include "harness.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "impulse/frontend/dump.h"
#include "impulse/frontend/lowering.h"
#include "impulse/frontend/parser.h"
#include "impulse/frontend/semantic.h"
#include "impulse/frontend/lexer.h"
#include "impulse/ir/analysis.h"
#include "impulse/ir/dump.h"
#include "impulse/runtime/runtime.h"

namespace impulse::tests::acceptance {
namespace {

using impulse::frontend::Lexer;
using impulse::frontend::Parser;
using impulse::frontend::Token;

struct PipelineOutputs {
    bool parse_success = false;
    bool semantic_success = false;
    bool runtime_invoked = false;
    impulse::runtime::VmStatus runtime_status = impulse::runtime::VmStatus::RuntimeError;
    bool runtime_has_value = false;
    double runtime_value = 0.0;
    std::string runtime_message;

    std::string tokens;
    std::string ast;
    std::string ir;
    std::string cfg;
    std::string ssa;
    std::string optimisation_log;
    std::string diagnostics;
    std::string runtime_trace;
    std::string runtime_summary;
};

[[nodiscard]] auto source_root() -> std::filesystem::path {
    static const std::filesystem::path root = std::filesystem::path{__FILE__}.parent_path();
    return root;
}

[[nodiscard]] auto cases_root() -> std::filesystem::path { return source_root() / "cases"; }

[[nodiscard]] auto read_text_file(const std::filesystem::path& path) -> std::optional<std::string> {
    std::ifstream stream(path);
    if (!stream) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

void append_diagnostics(std::string_view stage, const std::vector<impulse::frontend::Diagnostic>& diagnostics,
                        std::string& sink) {
    if (diagnostics.empty()) {
        return;
    }
    std::ostringstream out;
    out << stage << " diagnostics (" << diagnostics.size() << ")\n";
    for (const auto& diag : diagnostics) {
        out << "  [" << diag.location.line << ':' << diag.location.column << "] " << diag.message << '\n';
    }
    sink += out.str();
    if (!sink.empty() && sink.back() != '\n') {
        sink.push_back('\n');
    }
}

[[nodiscard]] auto join_module_path(const std::vector<std::string>& path) -> std::string {
    if (path.empty()) {
        return "<anonymous>";
    }
    std::ostringstream out;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (i != 0) {
            out << "::";
        }
        out << path[i];
    }
    return out.str();
}

[[nodiscard]] auto run_pipeline(const std::string& source, const std::optional<std::string>& stdin_text)
    -> PipelineOutputs {
    PipelineOutputs outputs;

    Lexer lexer(source);
    const std::vector<Token> tokens = lexer.tokenize();
    {
        std::ostringstream token_stream;
        impulse::frontend::dump_tokens(tokens, token_stream);
        outputs.tokens = token_stream.str();
    }

    Parser parser(source);
    auto parse_result = parser.parseModule();
    outputs.parse_success = parse_result.success;
    append_diagnostics("parse", parse_result.diagnostics, outputs.diagnostics);
    if (!parse_result.success) {
        outputs.ast = "<parse failed>\n";
        outputs.ir = "<parse failed>\n";
        outputs.cfg = "<parse failed>\n";
        outputs.ssa = "<parse failed>\n";
        outputs.optimisation_log = "<parse failed>\n";
        outputs.runtime_summary = "parse failure\n";
        return outputs;
    }

    {
        std::ostringstream ast_stream;
        impulse::frontend::dump_ast(parse_result.module, ast_stream);
        outputs.ast = ast_stream.str();
    }

    const auto semantic_result = impulse::frontend::analyzeModule(parse_result.module);
    outputs.semantic_success = semantic_result.success;
    append_diagnostics("semantic", semantic_result.diagnostics, outputs.diagnostics);
    if (!semantic_result.success) {
        outputs.ir = "<semantic analysis failed>\n";
        outputs.cfg = "<semantic analysis failed>\n";
        outputs.ssa = "<semantic analysis failed>\n";
        outputs.optimisation_log = "<semantic analysis failed>\n";
        outputs.runtime_summary = "semantic failure\n";
        return outputs;
    }

    const impulse::ir::Module lowered = impulse::frontend::lower_to_ir(parse_result.module);
    {
        std::ostringstream ir_stream;
        impulse::ir::dump_ir(lowered, ir_stream);
        outputs.ir = ir_stream.str();
    }

    const auto analyses = impulse::ir::analyse_module(lowered);
    {
        std::ostringstream cfg_stream;
        std::ostringstream ssa_stream;
        std::ostringstream log_stream;
        for (const auto& entry : analyses) {
            cfg_stream << "Function " << entry.name << '\n';
            impulse::ir::dump_cfg(entry.cfg, cfg_stream);
            cfg_stream << '\n';

            ssa_stream << "Function " << entry.name << "\n== Before optimisation ==\n";
            impulse::ir::dump_ssa(entry.ssa_before, ssa_stream, true);
            ssa_stream << "== After optimisation ==\n";
            impulse::ir::dump_ssa(entry.ssa_after, ssa_stream, true);
            ssa_stream << '\n';

            log_stream << "Function " << entry.name << '\n';
            for (const auto& line : entry.optimisation_log) {
                log_stream << "  " << line << '\n';
            }
            log_stream << '\n';
        }
        outputs.cfg = cfg_stream.str();
        outputs.ssa = ssa_stream.str();
        outputs.optimisation_log = log_stream.str();
    }

    impulse::runtime::Vm vm;
    std::istringstream stdin_stream;
    if (stdin_text.has_value()) {
        stdin_stream.str(*stdin_text);
        stdin_stream.clear();
        vm.set_input_stream(&stdin_stream);
    }
    std::ostringstream trace_stream;
    vm.set_trace_stream(&trace_stream);
    const auto load_result = vm.load(lowered);
    if (!load_result.success) {
        std::ostringstream out;
        out << "load failure (" << load_result.diagnostics.size() << ")\n";
        for (const auto& diag : load_result.diagnostics) {
            out << "  " << diag << '\n';
        }
        outputs.runtime_summary = out.str();
        outputs.runtime_trace = trace_stream.str();
        return outputs;
    }

    outputs.runtime_invoked = true;
    const std::string module_name = join_module_path(lowered.path);
    const auto result = vm.run(module_name, "main");
    vm.set_trace_stream(nullptr);
    if (stdin_text.has_value()) {
        vm.set_input_stream(nullptr);
    }
    outputs.runtime_trace = trace_stream.str();
    outputs.runtime_status = result.status;
    outputs.runtime_has_value = result.has_value;
    outputs.runtime_value = result.value;
    outputs.runtime_message = result.message;

    std::ostringstream summary;
    summary << "status=" << static_cast<int>(result.status);
    if (result.has_value) {
        summary << " value=" << result.value;
    }
    if (!result.message.empty()) {
        summary << " message='" << result.message << '\'';
    }
    summary << '\n';
    outputs.runtime_summary = summary.str();
    return outputs;
}

[[nodiscard]] auto collect_cases() -> std::vector<std::filesystem::path> {
    std::vector<std::filesystem::path> cases;
    const auto root = cases_root();
    if (!std::filesystem::exists(root)) {
        return cases;
    }
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (!entry.is_directory()) {
            continue;
        }
        const auto program = entry.path() / "program.impulse";
        if (std::filesystem::exists(program)) {
            cases.push_back(entry.path());
        }
    }
    std::sort(cases.begin(), cases.end());
    return cases;
}

[[nodiscard]] auto compare_output(const std::filesystem::path& expected_path, const std::string& actual)
    -> std::optional<std::string> {
    const auto expected = read_text_file(expected_path);
    if (!expected.has_value()) {
        std::ostringstream out;
        out << "missing expectation: " << expected_path.filename().string();
        return out.str();
    }
    if (*expected == actual) {
        return std::nullopt;
    }

    std::istringstream exp_stream(*expected);
    std::istringstream act_stream(actual);
    std::string exp_line;
    std::string act_line;
    std::size_t line = 1;
    while (true) {
        const bool exp_ok = static_cast<bool>(std::getline(exp_stream, exp_line));
        const bool act_ok = static_cast<bool>(std::getline(act_stream, act_line));
        if (!exp_ok && !act_ok) {
            break;
        }
        if (!exp_ok || !act_ok) {
            return std::string{"length mismatch at line "} + std::to_string(line);
        }
        if (exp_line != act_line) {
            std::ostringstream diff;
            diff << "line " << line << " differs\n  expected: " << exp_line << "\n    actual: " << act_line;
            return diff.str();
        }
        ++line;
    }

    return std::string{"content differs (unknown cause)"};
}

[[nodiscard]] auto expected_definitions() -> std::vector<std::pair<std::string, std::string>> {
    return {
        {"tokens", "expected.tokens.txt"},
        {"ast", "expected.ast.txt"},
        {"ir", "expected.ir.txt"},
        {"cfg", "expected.cfg.txt"},
        {"ssa", "expected.ssa.txt"},
        {"optimisation log", "expected.optimisation.txt"},
        {"runtime trace", "expected.runtime-trace.txt"},
        {"runtime result", "expected.runtime.txt"},
        {"diagnostics", "expected.diagnostics.txt"},
    };
}

struct CaseData {
    std::string name;
    std::filesystem::path program_path;
    std::optional<std::string> stdin_text;
    bool stdin_load_error = false;
};

[[nodiscard]] auto load_case(const std::filesystem::path& path) -> CaseData {
    CaseData data;
    data.name = path.filename().string();
    data.program_path = path / "program.impulse";
    const auto stdin_path = path / "stdin.txt";
    if (std::filesystem::exists(stdin_path)) {
        data.stdin_text = read_text_file(stdin_path);
        if (!data.stdin_text.has_value()) {
            data.stdin_load_error = true;
        }
    }
    return data;
}

}  // namespace

auto run_suite() -> std::vector<CaseReport> {
    std::vector<CaseReport> reports;
    const auto case_paths = collect_cases();
    reports.reserve(case_paths.size());

    for (const auto& case_path : case_paths) {
        const auto data = load_case(case_path);
        CaseReport report;
        report.name = data.name;

        if (data.stdin_load_error) {
            report.messages.emplace_back("failed to read stdin.txt");
            reports.push_back(std::move(report));
            continue;
        }

        const auto source = read_text_file(data.program_path);
        if (!source.has_value()) {
            report.messages.emplace_back("failed to read program.impulse");
            reports.push_back(std::move(report));
            continue;
        }

        const auto outputs = run_pipeline(*source, data.stdin_text);
        bool success = true;

        const auto expectation_map = expected_definitions();
        for (const auto& [label, filename] : expectation_map) {
            const auto expected_path = case_path / filename;
            const std::string* actual = nullptr;
            if (label == "tokens") {
                actual = &outputs.tokens;
            } else if (label == "ast") {
                actual = &outputs.ast;
            } else if (label == "ir") {
                actual = &outputs.ir;
            } else if (label == "cfg") {
                actual = &outputs.cfg;
            } else if (label == "ssa") {
                actual = &outputs.ssa;
            } else if (label == "optimisation log") {
                actual = &outputs.optimisation_log;
            } else if (label == "runtime trace") {
                actual = &outputs.runtime_trace;
            } else if (label == "runtime result") {
                actual = &outputs.runtime_summary;
            } else if (label == "diagnostics") {
                actual = &outputs.diagnostics;
            }

            if (actual == nullptr) {
                continue;
            }

            const auto diff = compare_output(expected_path, *actual);
            if (diff.has_value()) {
                success = false;
                std::ostringstream message;
                message << label << ": " << *diff;
                report.messages.emplace_back(message.str());
            }
        }

        report.success = success;
        reports.push_back(std::move(report));
    }

    return reports;
}

}  // namespace impulse::tests::acceptance
