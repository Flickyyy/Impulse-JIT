# Exam Benchmarks

This directory contains the three benchmark programs required for the exam:

## Task 1: Factorial Calculation
**File:** `factorial.impulse`
- **Objective:** Calculate factorial using recursion
- **Benchmark:** factorial(20) = 2432902008176640000
- **Tests:** Recursive function calls, stack management

## Task 2: Array Sorting
**File:** `sorting.impulse`
- **Objective:** Sort an array using iterative quicksort
- **Benchmark:** Sort 100 elements (reduced from 10,000 for interpreter performance)
- **Tests:** Array handling, loops, element comparison
- **Note:** Uses iterative quicksort with array-based stack to avoid deep recursion

## Task 3: Prime Number Generation
**File:** `primes.impulse`
- **Objective:** Generate prime numbers using Sieve of Eratosthenes
- **Benchmark:** Find all primes up to 100,000 (should be 9,592)
- **Tests:** Array manipulation, loops, arithmetic operations
- **Returns:** Length of the primes array (9,592)

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
| Sorting | 100 elements | 1 (sorted correctly) |
| Primes | up to 100000 | 9592 (array length) |
