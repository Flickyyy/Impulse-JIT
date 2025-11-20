# Impulse-JIT

Минимальный учебный компилятор: C++ парсер/IR + VM, Go CLI.

## Быстрый старт

```bash
cmake -S . -B build
cmake --build build
cd cli && go build ./cmd/impulsec
```

## CLI

```bash
./cli/impulsec --file code.imp [--emit-ir|--check|--evaluate|--run]
```

## Что работает

- Парсинг: module/import/export, let/const/var, func, struct, interface
- Операторы: арифметика (+,-,*,/,%), логика (&&,||,!), сравнения, унарный минус
- Выполнение: константы и простые функции с return

## Тесты

```bash
cmake --build build --target impulse-tests && cd build && ctest
cd ../cli && go test ./...
```
