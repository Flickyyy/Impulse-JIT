# Runtime Performance Analysis

## Critical Bottlenecks (High Impact)

### 1. **String Allocation in `store_value()` - CRITICAL**
**Location:** `runtime/include/impulse/runtime/ssa_interpreter.h:128`
```cpp
const std::string storage_key = "$ssa:" + value.to_string();
locals_[storage_key] = data;
```
**Impact:** Called for EVERY instruction that writes a value. Creates a new string allocation on every store.
- `value.to_string()` allocates: `"v" + std::to_string(symbol) + "." + std::to_string(version)`
- String concatenation: `"$ssa:" + ...` creates another allocation
- Hash map insertion with string key

**Frequency:** ~50-90% of all instructions (most instructions write results)
**Estimated Cost:** 2-3 allocations + hash computation per store

**Solution:** 
- Remove or make conditional (only if debugging/tracing)
- Use integer key instead of string key
- Pre-compute keys if needed

---

### 2. **Linear Search in `find_symbol()` - HIGH**
**Location:** `ir/src/ssa.cpp:543-549`
```cpp
auto SsaFunction::find_symbol(SymbolId id) const -> const SsaSymbol* {
    for (const auto& symbol : symbols) {
        if (symbol.id == id) {
            return &symbol;
        }
    }
    return nullptr;
}
```
**Impact:** Called in `lookup_value()` and `store_value()` for every version==0 value lookup.
- Linear O(n) search through symbols vector
- Called multiple times per instruction

**Frequency:** ~10-30% of value lookups (when version == 0)
**Estimated Cost:** O(n) where n = number of symbols

**Solution:**
- Build a `std::unordered_map<SymbolId, const SsaSymbol*>` index during SSA construction
- Or use direct array access if symbols are dense

---

### 3. **std::function Indirection in Opcode Dispatch - HIGH**
**Location:** `runtime/src/ssa_interpreter.cpp:151-153`
```cpp
const auto handler_it = opcode_handlers_.find(inst.opcode);
if (handler_it != opcode_handlers_.end()) {
    return handler_it->second(block, inst, current, previous, jumped);
}
```
**Impact:** Called for EVERY instruction execution.
- Hash map lookup for opcode
- `std::function` call overhead (virtual call + potential heap allocation)
- Lambda capture overhead

**Frequency:** 100% of instructions
**Estimated Cost:** Hash lookup + function pointer indirection + potential heap access

**Solution:**
- Use direct function pointer table or switch statement
- Use `if-else` chain for common opcodes (compiler can optimize)
- Consider using `std::array` with opcode enum as index

---

### 4. **Multiple Hash Map Lookups in `lookup_value()` - MEDIUM-HIGH**
**Location:** `runtime/include/impulse/runtime/ssa_interpreter.h:90-118`
```cpp
// 1. value_cache_ lookup
const auto it = value_cache_.find(key);

// 2. ssa_.find_symbol() - linear search
if (const auto* symbol = ssa_.find_symbol(value.symbol); ...) {
    // 3. locals_ lookup
    if (const auto localIt = locals_.find(symbol->name); ...)
    // 4. parameters_ lookup  
    if (const auto paramIt = parameters_.find(symbol->name); ...)
    // 5. globals_ lookup
    if (const auto globalIt = globals_.find(symbol->name); ...)
}
```
**Impact:** Called for EVERY instruction that reads values (most instructions).
- Up to 5 lookups/searches per value lookup
- String hash computation for name-based lookups

**Frequency:** ~80-100% of instructions (most read operands)
**Estimated Cost:** 1-5 hash lookups + 1 linear search

**Solution:**
- Cache symbol lookups
- Use direct indexing where possible
- Combine locals/parameters/globals into single map with priority

---

### 5. **Linear Search for Function Lookup - MEDIUM**
**Location:** `runtime/src/ssa_interpreter.cpp:372-378`
```cpp
const ir::Function* target_func = nullptr;
for (const auto& f : functions_) {
    if (f.name == callee_name) {
        target_func = &f;
        break;
    }
}
```
**Impact:** Called for every function call (non-builtin).
- Linear O(n) search through functions vector
- String comparison for each function

**Frequency:** Every function call (depends on program)
**Estimated Cost:** O(n) where n = number of functions

**Solution:**
- Build `std::unordered_map<std::string, const ir::Function*>` index
- Or pass function index instead of name

---

### 6. **String Operations in `value.to_string()` - MEDIUM**
**Location:** `ir/include/impulse/ir/ssa.h:20-22`
```cpp
[[nodiscard]] auto to_string() const -> std::string {
    return "v" + std::to_string(symbol) + "." + std::to_string(version);
}
```
**Impact:** Called in `store_value()` for every write (via storage_key creation).
- Multiple string allocations
- `std::to_string()` calls

**Frequency:** Every `store_value()` call
**Estimated Cost:** 3-4 allocations per call

**Solution:**
- Remove storage_key entirely (see #1)
- Or cache string representations

---

### 7. **Binary Op Handler Hash Map Lookup - MEDIUM**
**Location:** `runtime/src/ssa_interpreter.cpp:236-238`
```cpp
const auto op_handler_it = binary_op_handlers_.find(op);
if (op_handler_it != binary_op_handlers_.end()) {
    return op_handler_it->second(left, right, inst.result);
}
```
**Impact:** Called for every binary operation.
- Hash map lookup + std::function call

**Frequency:** ~20-40% of instructions (binary ops)
**Estimated Cost:** Hash lookup + function call

**Solution:**
- Use switch statement or if-else chain for common operators
- Direct function calls instead of std::function

---

### 8. **Phi Materialization Linear Search - MEDIUM**
**Location:** `runtime/src/ssa_interpreter.cpp:112-114`
```cpp
const auto it = std::find_if(phi.inputs.begin(), phi.inputs.end(), [&](const ir::PhiInput& input) {
    return input.predecessor == *previous;
});
```
**Impact:** Called for every block entry with phi nodes.
- Linear search through phi inputs

**Frequency:** Every block with phi nodes (common in loops)
**Estimated Cost:** O(m) where m = number of phi inputs

**Solution:**
- Build predecessor -> value map for phi nodes
- Or use direct indexing if predecessors are dense

---

## Moderate Bottlenecks

### 9. **String Concatenation in Builtin Functions**
**Location:** `runtime/src/ssa_interpreter.cpp:648-651` (print/println)
```cpp
std::ostringstream text;
for (std::size_t i = 0; i < args.size(); ++i) {
    if (i != 0) text << ' ';
    text << format_value_for_output(args[i]);
}
```
**Impact:** String formatting for I/O operations
**Frequency:** Every print/println call
**Solution:** Use string_view where possible, reserve capacity

---

### 10. **Vector Resize in Function Calls**
**Location:** `runtime/src/ssa_interpreter.cpp:374`
```cpp
std::vector<Value> args;
args.reserve(arg_count);
for (const auto& arg : inst.arguments) {
    args.push_back(*value);
}
```
**Impact:** Vector allocation and copying
**Frequency:** Every function call
**Solution:** Already using reserve, but could use array_view or span

---

## Summary by Impact

| Rank | Issue | Impact | Frequency | Fix Difficulty |
|------|-------|--------|-----------|----------------|
| 1 | String alloc in `store_value()` | CRITICAL | ~50-90% of instructions | Easy |
| 2 | Linear `find_symbol()` | HIGH | ~10-30% of lookups | Medium |
| 3 | `std::function` opcode dispatch | HIGH | 100% of instructions | Medium |
| 4 | Multiple hash lookups | MEDIUM-HIGH | ~80-100% of instructions | Medium |
| 5 | Linear function lookup | MEDIUM | Every function call | Easy |
| 6 | `value.to_string()` calls | MEDIUM | Every store | Easy (fixes with #1) |
| 7 | Binary op hash lookup | MEDIUM | ~20-40% of instructions | Easy |
| 8 | Phi linear search | MEDIUM | Every block with phi | Medium |

---

## Recommended Optimization Priority

1. **Remove storage_key string allocation** - Biggest win, easiest fix
2. **Add symbol index map** - High impact, medium effort
3. **Replace std::function with direct calls** - High impact, medium effort
4. **Add function lookup map** - Medium impact, easy fix
5. **Optimize binary op dispatch** - Medium impact, easy fix
6. **Optimize phi materialization** - Medium impact, medium effort

---

## Expected Performance Gains

- **Fix #1 (storage_key)**: 20-40% improvement (removes 50-90% of allocations)
- **Fix #2 (symbol index)**: 5-15% improvement
- **Fix #3 (opcode dispatch)**: 10-20% improvement
- **Combined fixes**: 40-60% overall improvement expected

