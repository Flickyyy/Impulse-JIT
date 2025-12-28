# Демонстрация Impulse-JIT

## Быстрый запуск

```bash
# Сборка (Release)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Все тесты (90 тестов, ~3 сек)
./build/tests/impulse-tests
```

## Бенчмарки для показа

```bash
# Факториал (20!) - мгновенно
./build/tools/cpp-cli/impulse-cpp --file benchmarks/factorial.impulse --run
# Результат: 2432902008176640000

# Простые числа (100K) - ~180ms
time ./build/tools/cpp-cli/impulse-cpp --file benchmarks/primes.impulse --run
# Результат: 9592 простых чисел

# Сортировка (1000 элементов) - ~650ms  
time ./build/tools/cpp-cli/impulse-cpp --file benchmarks/sorting.impulse --run
# Результат: Is sorted: 1

# N-Body симуляция - ~700ms
time ./build/tools/cpp-cli/impulse-cpp --file benchmarks/nbody.impulse --run
```

## JIT Демонстрация (5-9x ускорение)

```bash
# Показать JIT тесты с красивым выводом
./build/tests/impulse-tests --gtest_filter="JitPerformance*"
```

Вывод покажет:
```
=== Large Arithmetic Computation ===
Interpreter time: 48ms
JIT time: 6ms
Speedup: 8.00x
✓ JIT provides significant speedup!
```

## GC Демонстрация

```bash
# GC тесты
./build/tests/impulse-tests --gtest_filter="RuntimeTest.*Gc*"
```

## Профилировщик

```bash
# Профилирование сортировки
./build/tests/impulse-tests --gtest_filter="SortingProfile*"
```

## Ключевые файлы для показа кода

| Компонент | Файл |
|-----------|------|
| JIT компилятор | `jit/src/jit.cpp` |
| GC (mark-sweep) | `runtime/src/gc_heap.cpp` |
| SSA интерпретатор | `runtime/src/ssa_interpreter.cpp` |
| Enum dispatch | `ir/include/impulse/ir/ssa.h` (SsaOpcode, BinaryOp) |

## Оптимизации

1. **Enum-based dispatch** (~2x ускорение интерпретатора)
   - `SsaOpcode` enum вместо строк для opcodes
   - `BinaryOp` enum вместо строк для операторов
   - Switch-case вместо if-else chains

2. **JIT компиляция** (5-9x ускорение)
   - x86-64 native code generation
   - SSE инструкции для float операций
   - Кеширование скомпилированного кода

3. **Кеширование**
   - SSA кеш для hot functions
   - JIT кеш для скомпилированного кода
   - Function lookup cache

## Статистика

- **90 тестов** в 16 test suites
- **JIT speedup**: 5-9x для численных вычислений
- **Interpreter speedup**: ~2x от enum dispatch
