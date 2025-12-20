#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "impulse/frontend/dump.h"
#include "impulse/frontend/lexer.h"
#include "impulse/frontend/lowering.h"
#include "impulse/frontend/parser.h"
#include "impulse/frontend/semantic.h"
#include "impulse/ir/analysis.h"
#include "impulse/ir/dump.h"
#include "impulse/runtime/runtime.h"

namespace {

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

[[nodiscard]] auto read_text_file(const std::filesystem::path& path) -> std::optional<std::string> {
    std::ifstream stream(path);
    if (!stream) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
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

[[nodiscard]] auto run_pipeline(const std::string& source, const std::optional<std::string>& stdin_text)
    -> PipelineOutputs {
    PipelineOutputs outputs;

    impulse::frontend::Lexer lexer(source);
    const std::vector<impulse::frontend::Token> tokens = lexer.tokenize();
    {
        std::ostringstream token_stream;
        impulse::frontend::dump_tokens(tokens, token_stream);
        outputs.tokens = token_stream.str();
    }

    impulse::frontend::Parser parser(source);
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

void write_case_outputs(const std::filesystem::path& case_path, const PipelineOutputs& outputs) {
    const std::vector<std::pair<std::string_view, const std::string*>> files = {
        {"expected.tokens.txt", &outputs.tokens},
        {"expected.ast.txt", &outputs.ast},
        {"expected.ir.txt", &outputs.ir},
        {"expected.cfg.txt", &outputs.cfg},
        {"expected.ssa.txt", &outputs.ssa},
        {"expected.optimisation.txt", &outputs.optimisation_log},
        {"expected.runtime-trace.txt", &outputs.runtime_trace},
        {"expected.runtime.txt", &outputs.runtime_summary},
        {"expected.diagnostics.txt", &outputs.diagnostics},
    };

    for (const auto& [filename, content] : files) {
        const auto output_path = case_path / std::string{filename};
        std::ofstream file(output_path, std::ios::out | std::ios::trunc);
        if (!file) {
            std::cerr << "Failed to open " << output_path << " for writing\n";
            continue;
        }
        file << *content;
    }
}

void generate_for_case(const std::filesystem::path& case_path) {
    const auto program_path = case_path / "program.impulse";
    const auto source = read_text_file(program_path);
    if (!source.has_value()) {
        std::cerr << "Skipping case '" << case_path.filename().string() << "': unable to read program.impulse\n";
        return;
    }
    std::optional<std::string> stdin_text;
    const auto stdin_path = case_path / "stdin.txt";
    if (std::filesystem::exists(stdin_path)) {
        stdin_text = read_text_file(stdin_path);
        if (!stdin_text.has_value()) {
            std::cerr << "Skipping case '" << case_path.filename().string() << "': unable to read stdin.txt\n";
            return;
        }
    }
    const auto outputs = run_pipeline(*source, stdin_text);
    write_case_outputs(case_path, outputs);
    std::cout << "Generated expectations for " << case_path << '\n';
}

}  // namespace

auto main(int argc, char** argv) -> int {
    std::filesystem::path cases_root = "tests/acceptance/cases";
    if (argc > 1) {
        cases_root = argv[1];
    }

    if (!std::filesystem::exists(cases_root)) {
        std::cerr << "Cases directory not found: " << cases_root << '\n';
        return 1;
    }

    std::vector<std::filesystem::path> case_paths;
    for (const auto& entry : std::filesystem::directory_iterator(cases_root)) {
        if (!entry.is_directory()) {
            continue;
        }
        const auto program = entry.path() / "program.impulse";
        if (std::filesystem::exists(program)) {
            case_paths.push_back(entry.path());
        }
    }

    std::sort(case_paths.begin(), case_paths.end());
    for (const auto& path : case_paths) {
        generate_for_case(path);
    }

    return 0;
}
