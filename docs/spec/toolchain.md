# Toolchain & Processes

## 1. Компоненты
- `frontend/` — **C++ by default** (PEGTL/ANTLR) для лексера, парсера, type-check; Go-песочницы допускаются для генерации тестов.
- `ir/` — собственный SSA builder и оптимизации на C++, общий API с Go-драйверами.
- `runtime/` — VM, GC, JIT на C++ без внешних зависимостей;
- `cli/impulsec` — Go-утилита обёртка (CLI, реплаи, визуализации), общается с C++ через FFI.
- `tools/` — Python/Go скрипты для тестов, генераторов, бенчмарков.

## 2. Сборка
- Основной build: CMake (для C++), Go modules.
- Цели: `impulsec`, `impulse-vm`, `impulse-tests`.
- Пакеты/директивы:
	- `frontend` и `ir` собираются как статические библиотеки, экспортирующие C API.
	- `runtime` линкуется с JIT/GC и предоставляет `libimpulsevm.so`.
	- Go CLI линкится с C API через `cgo`.

## 3. Стиль и проверки
- Google C++ Style (`.clang-format`), `gofmt`.
- Lint и unit-тесты в CI (GitHub Actions / GitLab CI).
- C++ компилятор: clang++17+. Флаги: `-Wall -Wextra -Werror -pedantic`.
- Go версия: 1.22+. Модули без vendor.

## 4. Тестирование
- Golden tests для parser/typechecker.
- IR verifier + VM regression suite.
- Производственные бенчмарки factorial/sort/primes.
- Отдельные стресс-тесты GC (утечки, фрагментация).
- FFI smoke tests (Go ↔ C++), если задействованы.

## 5. Roadmap синхронизаций
- Каждое изменение языка сопровождается PR с обновлением `docs/spec/*`.
- Еженедельные demo-run бенчмарков.
- После перехода компонента на C++ bootstrap на Go считается устаревшим (оставляем только для reference).

## 6. Политика выбора языка
- Если компонент можно написать на "голом" C++ без сторонних библиотек — пишем на C++.
- Go используем для быстрых прототипов, CLI и tooling, но код должен быть перенесён в C++ перед мейлстоун "GC & Runtime".
- Python допускается только для вспомогательных скриптов (не часть тулчейна).

## 7. Структура репозитория
```
Impulse-JIT/
	frontend/
	ir/
	runtime/
	cli/
	tools/
	docs/spec/
	tests/
	benchmarks/
	scripts/
	cmake/
```

## 8. Git workflow и релизы
- Основная ветка `main` всегда зелёная, защищена (требует review + прохождение CI).
- Фичи разрабатываются в ветках `feature/<name>`; крупные работы делятся на несколько PR.
- Для каждой итерации roadmap создаём milestone и релизные теги `v0.x.y`.
- Релизный артефакт: архив с `impulsec`, `impulse-vm`, stdlib и примерами.
- Файлы автогенерации (Docs, схемы) собираются в `docs/build/`, не коммитятся.

## 9. CI/CD
- GitHub Actions: матрица по Ubuntu 22.04 / clang++17.
- Джобы: `fmt-check`, `lint`, `build`, `tests`, `benchmarks` (smoke), `docs-lint`.
- При merge в `main` автоматически публикуем nightly-артефакты (через GitHub Releases).

## 10. Настройка окружения
- Требуется установленный clang++17, CMake ≥ 3.24, Go 1.22.
- Для локальной разработки: `scripts/bootstrap.sh` — ставит pre-commit hooks, подтягивает зависимости.
- Предусмотрим Dockerfile со всем стеком для CI/локальных запусков.

## 11. План на TODO
- **Опкоды IR** → `docs/spec/ir.md` (таблица, бинарный формат).
- **ABI/FFI подробности** → `docs/spec/runtime.md`.
- **`.clang-format`, `.clang-tidy`, `.golangci.yml`** — добавить в корень.
- **CMake skeleton**: `cmake/toolchain.cmake`, `CMakeLists.txt` в каждом модуле.
