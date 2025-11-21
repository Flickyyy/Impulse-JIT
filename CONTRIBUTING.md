# Contributing to Impulse JIT Compiler

Thank you for your interest in contributing to Impulse! This document provides guidelines for contributing to the project.

## Quick Start

1. **Fork** the repository
2. **Clone** your fork: `git clone https://github.com/YOUR_USERNAME/Impulse-JIT.git`
3. **Build** the project: `cmake -S . -B build && cmake --build build`
4. **Run tests**: `./build/tests/impulse-tests`
5. **Make changes** and test them
6. **Submit** a pull request

## Development Workflow

### Building

```bash
# Configure build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

# Build all targets
cmake --build build

# Build specific target
cmake --build build --target impulse-tests
```

### Running Tests

```bash
# Run C++ unit tests
./build/tests/impulse-tests

# Test the CLI
cd cli && go test ./...
```

### Code Style

- **C++**: Follow the existing style (defined in `.clang-format`)
- **Go**: Use `gofmt` and `golint`
- **Comments**: Write clear, concise comments for complex logic
- **Naming**: Use descriptive names for functions and variables

### Testing Guidelines

When adding new features:

1. **Add tests** in the appropriate test group (see `tests/main.cpp`)
2. **Test edge cases**: empty input, zero values, error conditions
3. **Verify behavior**: across parser, IR, and runtime layers
4. **Update test count** in the test group header

Current test organization:
- **Lexer Tests**: Token recognition and parsing
- **Parser Tests**: AST construction and syntax validation
- **Semantic Tests**: Type checking and scope resolution
- **IR Tests**: IR generation and formatting
- **Operator Tests**: All operators (arithmetic, logical, unary)
- **Control Flow Tests**: if/else, while statements
- **Runtime Tests**: VM execution and function calls
- **Function Call Tests**: Parameter passing and nested calls

### Documentation

Update documentation when changing:

- **README.md**: Quick start and basic usage
- **STATUS.md**: Implementation status checklist
- **docs/spec/grammar.md**: Language syntax changes
- **docs/spec/frontend.md**: Parser implementation changes
- **docs/spec/ir.md**: IR instruction changes
- **docs/spec/runtime.md**: VM behavior changes

### Pull Request Process

1. **Create a descriptive branch**: `feature/add-for-loops` or `fix/parser-crash`
2. **Write clear commit messages**: "Add support for for-loops" not "fix stuff"
3. **Keep PRs focused**: One feature or fix per PR
4. **Test thoroughly**: All tests must pass
5. **Update documentation**: Keep docs in sync with code
6. **Add examples**: Show how new features work

### Commit Message Format

```
<type>: <short summary>

<detailed description if needed>

Examples:
- feat: Add for-loop parsing and execution
- fix: Correct parameter passing in function calls
- docs: Update grammar specification for new syntax
- test: Add comprehensive operator precedence tests
- refactor: Simplify IR lowering for binary operations
```

## Areas for Contribution

### High Priority
- **Loop controls**: Implement `break` / `continue` semantics
- **Type checking**: Implement semantic type validation
- **Recursion**: Add support for recursive function calls
- **Error recovery**: Improve parser error recovery

### Medium Priority
- **Standard library**: Add print, file I/O functions
- **Optimization**: Basic constant folding and dead code elimination
- **Better diagnostics**: More helpful error messages with suggestions

### Low Priority (Future)
- **Generics**: Generic type support
- **Pattern matching**: Match expressions
- **JIT compilation**: Native code generation
- **Garbage collector**: Memory management

## Questions?

- **Open an issue** for bugs or feature requests
- **Start a discussion** for design questions
- **Check STATUS.md** for current implementation status

## Code of Conduct

- Be respectful and constructive
- Focus on technical merit
- Help newcomers learn
- Write code you'd want to maintain

Happy coding! 🚀
