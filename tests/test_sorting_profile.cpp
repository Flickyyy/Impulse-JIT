#include <gtest/gtest.h>
#include <fstream>
#include <iostream>
#include <filesystem>

#include "../frontend/include/impulse/frontend/lowering.h"
#include "../frontend/include/impulse/frontend/parser.h"
#include "../frontend/include/impulse/frontend/semantic.h"
#include "../runtime/include/impulse/runtime/runtime.h"

using namespace impulse::runtime;

namespace {
[[nodiscard]] auto get_benchmarks_dir() -> std::filesystem::path {
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
}  // namespace

TEST(SortingProfileTest, ProfileSortingBenchmark) {
    const auto benchmarks_dir = get_benchmarks_dir();
    const auto sorting_path = benchmarks_dir / "sorting.impulse";
    
    const auto source = read_file(sorting_path);
    ASSERT_TRUE(source.has_value()) << "Failed to read sorting.impulse";
    
    impulse::frontend::Parser parser(*source);
    auto parse_result = parser.parseModule();
    ASSERT_TRUE(parse_result.success);
    
    const auto semantic = impulse::frontend::analyzeModule(parse_result.module);
    ASSERT_TRUE(semantic.success);
    
    const auto lowered = impulse::frontend::lower_to_ir(parse_result.module);
    
    // Get module name
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
    
    // Create VM with profiling enabled
    Vm vm;
    vm.set_jit_enabled(false);  // Disable JIT to see interpreter performance
    vm.set_profiling_enabled(true);
    vm.reset_profiling();
    
    const auto load_result = vm.load(lowered);
    ASSERT_TRUE(load_result.success);
    
    std::cout << "\n=== Running sorting benchmark with profiling ===\n";
    const auto start_time = std::chrono::high_resolution_clock::now();
    
    const auto result = vm.run(module_name, "main");
    
    const auto end_time = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    EXPECT_EQ(result.status, VmStatus::Success);
    EXPECT_TRUE(result.has_value);
    
    std::cout << "\nTotal execution time: " << duration.count() << " ms\n";
    std::cout << "\n=== Profiling Results ===\n";
    vm.dump_profiling_results(std::cout);
}

