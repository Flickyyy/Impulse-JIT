# Runtime Profiling Guide

The Impulse JIT runtime includes a built-in profiling system that allows you to measure execution time for each function, identify bottlenecks, and compare JIT vs interpreter performance.

## Quick Start

```cpp
#include "impulse/runtime/runtime.h"

// Create VM and enable profiling
impulse::runtime::Vm vm;
vm.set_profiling_enabled(true);
vm.reset_profiling();  // Clear any previous profiling data

// Load and run your program
auto load_result = vm.load(module);
auto result = vm.run(module_name, "main");

// Get profiling results
std::string profile = vm.get_profiling_results();
std::cout << profile << std::endl;

// Or dump directly to a stream
vm.dump_profiling_results(std::cout);
```

## API Reference

### Enable/Disable Profiling

```cpp
void set_profiling_enabled(bool enabled);
```

Enable or disable profiling. When enabled, the runtime tracks execution time for every function call.

### Reset Profiling Data

```cpp
void reset_profiling();
```

Clear all collected profiling data. Call this before running a new benchmark to start fresh.

### Get Profiling Results

```cpp
std::string get_profiling_results() const;
```

Returns a formatted string with profiling statistics.

### Dump Profiling Results

```cpp
void dump_profiling_results(std::ostream& out) const;
```

Writes profiling results directly to an output stream.

## Profiling Output Format

The profiling output includes:

- **Function Name**: Full name in format `module::function`
- **Calls**: Number of times the function was called
- **Total (ms)**: Total time spent in the function across all calls
- **Avg (ms)**: Average time per call
- **Min (ns)**: Fastest execution time
- **Max (ns)**: Slowest execution time
- **JIT**: Whether the function was JIT compiled (Yes/No)
- **% Total**: Percentage of total execution time spent in this function

Functions are sorted by total time (descending), so bottlenecks appear at the top.

## Example Output

```
=== Function Profiling Results ===

Total functions profiled: 3
Total calls: 10001
Total time: 45 ms

Function Name                            Calls      Total (ms)    Avg (ms)      Min (ns)      Max (ns)      JIT        % Total
----------------------------------------------------------------------------------------------------------------------------------------------------
test::accumulate                              1           40           40.000        40000000     40000000     No         88.89%
test::compute                             10000            4            0.000          300          500     Yes         8.89%
test::main                                   1            1            1.000        1000000      1000000     No         2.22%
```

## Use Cases

### 1. Identify Bottlenecks

```cpp
vm.set_profiling_enabled(true);
vm.reset_profiling();
vm.run(module_name, "main");

// Functions at the top of the report are your bottlenecks
vm.dump_profiling_results(std::cout);
```

### 2. Compare JIT vs Interpreter

```cpp
// With JIT
{
    Vm vm;
    vm.set_jit_enabled(true);
    vm.set_profiling_enabled(true);
    vm.reset_profiling();
    vm.run(module_name, "main");
    std::cout << "=== With JIT ===\n";
    vm.dump_profiling_results(std::cout);
}

// Without JIT
{
    Vm vm;
    vm.set_jit_enabled(false);
    vm.set_profiling_enabled(true);
    vm.reset_profiling();
    vm.run(module_name, "main");
    std::cout << "=== Without JIT ===\n";
    vm.dump_profiling_results(std::cout);
}
```

### 3. Measure Function Call Frequency

The profiling system tracks call counts, helping you identify:
- Hot functions (called many times)
- Cold functions (called rarely)
- Functions that might benefit from JIT compilation

### 4. Performance Regression Testing

```cpp
vm.set_profiling_enabled(true);
vm.reset_profiling();
vm.run(module_name, "main");

auto profile = vm.get_profiling_results();
// Save to file or compare with baseline
```

## Performance Impact

Profiling has minimal overhead:
- Uses high-resolution timers (`std::chrono::high_resolution_clock`)
- Only tracks function entry/exit (not individual instructions)
- Overhead is typically < 1% of execution time

## Tips

1. **Reset between runs**: Always call `reset_profiling()` before a new benchmark
2. **Focus on hot paths**: Functions with high call counts or high total time are optimization targets
3. **JIT indicators**: Functions marked "Yes" for JIT are compiled to native code
4. **Min/Max times**: Large differences indicate variable performance (may be due to cache effects, etc.)

## Limitations

- Profiling tracks function-level timing only (not individual instructions)
- Recursive functions will show cumulative time across all recursive calls
- The overhead of profiling itself is included in the measurements
- Profiling data is stored in memory and grows with the number of unique functions called

