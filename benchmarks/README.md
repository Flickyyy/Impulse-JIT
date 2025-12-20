# Exam Benchmarks

This directory contains the three benchmark programs required for the exam:

## Task 1: Factorial Calculation
**File:** `factorial.impulse`
- **Objective:** Calculate factorial using recursion
- **Benchmark:** factorial(20) = 2432902008176640000
- **Tests:** Recursive function calls, stack management

## Task 2: Array Sorting
**File:** `sorting.impulse`
- **Objective:** Sort an array using quicksort
- **Benchmark:** Sort 10,000 elements
- **Tests:** Array handling, loops, element comparison

## Task 3: Prime Number Generation
**File:** `primes.impulse`
- **Objective:** Generate prime numbers using trial division
- **Benchmark:** Find all primes up to 100,000 (should be 9,592)
- **Tests:** Array manipulation, loops, arithmetic operations

## Running Benchmarks

```bash
# From project root
./build/tools/cpp-cli/impulse-cpp --file benchmarks/factorial.impulse --run
./build/tools/cpp-cli/impulse-cpp --file benchmarks/sorting.impulse --run
./build/tools/cpp-cli/impulse-cpp --file benchmarks/primes.impulse --run
```

## Expected Results

| Benchmark | Input | Expected Output |
|-----------|-------|-----------------|
| Factorial | 20 | 2432902008176640000 |
| Sorting | 10000 elements | 1 (sorted correctly) |
| Primes | up to 100000 | 9592 primes |
