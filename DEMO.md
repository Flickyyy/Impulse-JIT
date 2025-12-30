# Демонстрация Impulse-JIT

## Быстрый запуск

```bash
# Сборка (Release)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Все быстрые тесты (125 тестов, ~4 сек)
./build/tests/impulse-tests --gtest_filter="-*Benchmark*:*Profile*"

# ВСЕ тесты (134 теста, ~30 сек)
./build/tests/impulse-tests
```

## Бенчмарки для показа

```bash
cd build

# Факториал (20!) - мгновенно
./tools/cpp-cli/impulse-cpp --file ../benchmarks/factorial.impulse --run --time
# Результат: 2432902008176640000

# Простые числа (100K) - ~3.6 сек
./tools/cpp-cli/impulse-cpp --file ../benchmarks/primes.impulse --run --time
# Результат: 9592 простых чисел

# Сортировка (5000 элементов) - ~2.6 сек
./tools/cpp-cli/impulse-cpp --file ../benchmarks/sorting.impulse --run --time
# Результат: Sorted 5000 elements: 1

# N-Body симуляция - ~12 сек
./tools/cpp-cli/impulse-cpp --file ../benchmarks/nbody.impulse --run --time
```

## JIT Демонстрация (5-6x ускорение)

```bash
# Показать JIT тесты с красивым выводом
./tests/impulse-tests --gtest_filter="JitPerformance*"
```

Вывод покажет:
```
=== Large Arithmetic Computation ===
Interpreter time: 647ms
JIT time: 116ms
Speedup: 5.58x
✓ JIT provides significant speedup!
```

## GC Демонстрация

```bash
cd build
# GC тесты
./tests/impulse-tests --gtest_filter="RuntimeTest.*Gc*"
```

## Профилировщик

```bash
cd build
# Профилирование сортировки (1K элементов)
./tests/impulse-tests --gtest_filter="SortingProfile*"
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

2. **JIT компиляция** (5-6x ускорение)
   - x86-64 native code generation
   - SSE инструкции для float операций
   - Кеширование скомпилированного кода

## Статистика

- **134 теста** в 19 test suites
- **44 edge case теста** для защиты
- **JIT**: 5-6x ускорение для straight-line кода

## CLI Опции

```bash
./tools/cpp-cli/impulse-cpp --help
```

Ключевые опции:
- `--run` - выполнить программу
- `--time` - показать время выполнения
- `--jit` / `--no-jit` - включить/выключить JIT
- `--dump-ssa` - показать SSA форму
- `--dump-cfg` - показать CFG
- **JIT speedup**: 5-9x для численных вычислений
- **Interpreter speedup**: ~2x от enum dispatch
