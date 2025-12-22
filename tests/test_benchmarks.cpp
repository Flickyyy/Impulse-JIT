#include <gtest/gtest.h>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#include "../frontend/include/impulse/frontend/lowering.h"
#include "../frontend/include/impulse/frontend/parser.h"
#include "../frontend/include/impulse/frontend/semantic.h"
#include "../runtime/include/impulse/runtime/runtime.h"

namespace {

[[nodiscard]] auto get_benchmarks_dir() -> std::filesystem::path {
    // Get the project root (assuming tests are in tests/ subdirectory)
    static const std::filesystem::path test_dir = std::filesystem::path{__FILE__}.parent_path();
    static const std::filesystem::path project_root = test_dir.parent_path();
    return project_root / "benchmarks";
}

[[nodiscard]] auto read_file(const std::filesystem::path& path) -> std::optional<std::string> {
    std::ifstream stream(path);
    if (!stream) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

[[nodiscard]] auto run_benchmark(const std::string& source) -> impulse::runtime::VmResult {
    impulse::frontend::Parser parser(source);
    auto parse_result = parser.parseModule();
    if (!parse_result.success) {
        impulse::runtime::VmResult error_result;
        error_result.status = impulse::runtime::VmStatus::RuntimeError;
        error_result.message = "Parse failed";
        return error_result;
    }

    const auto semantic = impulse::frontend::analyzeModule(parse_result.module);
    if (!semantic.success) {
        impulse::runtime::VmResult error_result;
        error_result.status = impulse::runtime::VmStatus::RuntimeError;
        error_result.message = "Semantic analysis failed";
        if (!semantic.diagnostics.empty()) {
            error_result.message += ": " + semantic.diagnostics[0].message;
        }
        return error_result;
    }

    const auto lowered = impulse::frontend::lower_to_ir(parse_result.module);
    impulse::runtime::Vm vm;
    const auto load_result = vm.load(lowered);
    if (!load_result.success) {
        impulse::runtime::VmResult error_result;
        error_result.status = impulse::runtime::VmStatus::RuntimeError;
        error_result.message = "Load failed";
        return error_result;
    }

    // Get module name from lowered IR
    std::string module_name;
    if (!lowered.path.empty()) {
        std::ostringstream name_stream;
        for (size_t i = 0; i < lowered.path.size(); ++i) {
            if (i != 0) {
                name_stream << "::";
            }
            name_stream << lowered.path[i];
        }
        module_name = name_stream.str();
    } else {
        module_name = "anonymous";
    }

    return vm.run(module_name, "main");
}

}  // namespace

TEST(BenchmarkTest, Factorial) {
    const auto benchmarks_dir = get_benchmarks_dir();
    const auto factorial_path = benchmarks_dir / "factorial.impulse";
    
    const auto source = read_file(factorial_path);
    ASSERT_TRUE(source.has_value()) << "Failed to read factorial.impulse";
    
    const auto result = run_benchmark(*source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success)
        << "Runtime error: " << result.message;
    EXPECT_TRUE(result.has_value);
    
    // Expected: factorial(20) = 2432902008176640000
    // Using approximate comparison due to floating point precision
    const double expected = 2432902008176640000.0;
    const double tolerance = 1e6;  // Allow for floating point precision loss
    EXPECT_NEAR(result.value, expected, tolerance)
        << "Expected factorial(20) ≈ " << expected 
        << ", got " << result.value;
}

TEST(BenchmarkTest, Primes) {
    const auto benchmarks_dir = get_benchmarks_dir();
    const auto primes_path = benchmarks_dir / "primes.impulse";
    
    const auto source = read_file(primes_path);
    ASSERT_TRUE(source.has_value()) << "Failed to read primes.impulse";
    
    // Measure execution time
    const auto start_time = std::chrono::high_resolution_clock::now();
    
    const auto result = run_benchmark(*source);
    
    const auto end_time = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success)
        << "Runtime error: " << result.message;
    EXPECT_TRUE(result.has_value);
    
    // Expected: 9,592 primes up to 100,000
    const double expected = 9592.0;
    EXPECT_DOUBLE_EQ(result.value, expected)
        << "Expected 9592 primes up to 100000, got " << result.value;
    
    // Log execution time
    std::cout << "Primes benchmark (Sieve of Eratosthenes, limit=100000): " 
              << duration.count() << " ms" << std::endl;
}

TEST(BenchmarkTest, Sorting) {
    const auto benchmarks_dir = get_benchmarks_dir();
    const auto sorting_path = benchmarks_dir / "sorting.impulse";
    
    const auto source = read_file(sorting_path);
    ASSERT_TRUE(source.has_value()) << "Failed to read sorting.impulse";
    
    // Measure execution time
    const auto start_time = std::chrono::high_resolution_clock::now();
    
    const auto result = run_benchmark(*source);
    
    const auto end_time = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success)
        << "Runtime error: " << result.message;
    EXPECT_TRUE(result.has_value);
    
    // The sorting benchmark returns 1 if sorted correctly, 0 otherwise
    const double expected = 1.0;
    EXPECT_DOUBLE_EQ(result.value, expected)
        << "Expected array to be sorted (1), got " << result.value;
    
    // Log execution time
    std::cout << "Sorting benchmark (Iterative Quicksort, 1000 elements): " 
              << duration.count() << " ms" << std::endl;
}

TEST(BenchmarkTest, FactorialParseAndSemantic) {
    const auto benchmarks_dir = get_benchmarks_dir();
    const auto factorial_path = benchmarks_dir / "factorial.impulse";
    
    const auto source = read_file(factorial_path);
    ASSERT_TRUE(source.has_value()) << "Failed to read factorial.impulse";
    
    impulse::frontend::Parser parser(*source);
    auto parse_result = parser.parseModule();
    ASSERT_TRUE(parse_result.success) << "Failed to parse factorial.impulse";
    
    const auto semantic = impulse::frontend::analyzeModule(parse_result.module);
    EXPECT_TRUE(semantic.success) << "Semantic analysis failed for factorial.impulse";
    EXPECT_TRUE(semantic.diagnostics.empty()) 
        << "Unexpected diagnostics in factorial.impulse";
}

TEST(BenchmarkTest, PrimesParseAndSemantic) {
    const auto benchmarks_dir = get_benchmarks_dir();
    const auto primes_path = benchmarks_dir / "primes.impulse";
    
    const auto source = read_file(primes_path);
    ASSERT_TRUE(source.has_value()) << "Failed to read primes.impulse";
    
    impulse::frontend::Parser parser(*source);
    auto parse_result = parser.parseModule();
    ASSERT_TRUE(parse_result.success) << "Failed to parse primes.impulse";
    
    const auto semantic = impulse::frontend::analyzeModule(parse_result.module);
    EXPECT_TRUE(semantic.success) << "Semantic analysis failed for primes.impulse";
    EXPECT_TRUE(semantic.diagnostics.empty()) 
        << "Unexpected diagnostics in primes.impulse";
}

TEST(BenchmarkTest, SortingParseAndSemantic) {
    const auto benchmarks_dir = get_benchmarks_dir();
    const auto sorting_path = benchmarks_dir / "sorting.impulse";
    
    const auto source = read_file(sorting_path);
    ASSERT_TRUE(source.has_value()) << "Failed to read sorting.impulse";
    
    impulse::frontend::Parser parser(*source);
    auto parse_result = parser.parseModule();
    ASSERT_TRUE(parse_result.success) << "Failed to parse sorting.impulse";
    
    const auto semantic = impulse::frontend::analyzeModule(parse_result.module);
    EXPECT_TRUE(semantic.success) << "Semantic analysis failed for sorting.impulse";
    EXPECT_TRUE(semantic.diagnostics.empty()) 
        << "Unexpected diagnostics in sorting.impulse";
}
