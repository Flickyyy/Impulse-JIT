#include <iostream>

#include "test_groups.h"

namespace {

void run_group(const char* name, int& total, int (*group_fn)()) {
    std::cout << '[' << name << "]\n";
    const int count = group_fn();
    total += count;
    std::cout << "  \u2713 " << count << " tests passed\n\n";
}

}  // namespace

auto main() -> int {
    std::cout << "==============================================\n";
    std::cout << "Impulse Compiler Test Suite\n";
    std::cout << "==============================================\n\n";

    int total_tests = 0;
    run_group("Lexer Tests", total_tests, runLexerTests);
    run_group("Parser Tests", total_tests, runParserTests);
    run_group("Semantic Tests", total_tests, runSemanticTests);
    run_group("IR Tests", total_tests, runIRTests);
    run_group("Operator Tests", total_tests, runOperatorTests);
    run_group("Control Flow Tests", total_tests, runControlFlowTests);
    run_group("Function Call Tests", total_tests, runFunctionCallTests);
    run_group("Runtime Tests", total_tests, runRuntimeTests);

    std::cout << "==============================================\n";
    std::cout << "All " << total_tests << " tests passed! \u2713\n";
    std::cout << "==============================================\n";
    return 0;
}
