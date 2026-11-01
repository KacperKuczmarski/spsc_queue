# spsc

A header-only, bounded, lock-free FIFO queue for exactly one producer thread and one consumer thread, written in C++20
using only the standard library.

## API

`kuczmars::SPSCQueue<T>` lives in `include/spsc/queue.hpp`.

| Operation               | Thread    | Behavior                                                                                  |
|-------------------------|-----------|-------------------------------------------------------------------------------------------|
| `SPSCQueue(capacity)`   | any       | Allocates room for `capacity` items. `explicit`; see the limitations for what it throws.  |
| `try_push(value)`       | producer  | Copies or moves `value` into the queue. Returns `false` if the queue is full.             |
| `try_emplace(args...)`  | producer  | Constructs an item in place from `args`. Returns `false` if the queue is full.            |
| `front()`               | consumer  | Returns a pointer to the oldest item, or `nullptr` if the queue is empty.                 |
| `pop()`                 | consumer  | Destroys and removes the oldest item. The queue must not be empty.                        |
| `try_pop()`             | consumer  | Removes the oldest item and returns it as `std::optional<T>`, or `std::nullopt` if empty. |
| `capacity()`            | any       | Returns the capacity given to the constructor, the maximum number of items.               |
| `size()`, `empty()`     | any       | Approximate number of items; see the limitations below.                                   |

`try_push`, `try_emplace` and `try_pop` are `[[nodiscard]]`: ignoring the result of a push silently drops the item, and
ignoring the result of `try_pop` loses it.

## How it works

Items live in a heap-allocated ring buffer of `capacity + 1` slots; the spare slot distinguishes a full queue from an
empty one. The producer owns the tail index and the consumer owns the head index; each publishes its index with a
release store and reads the other's with an acquire load. Each side also keeps a private copy of the other side's
index and only reloads the shared one when that copy says the queue is full (producer) or empty (consumer), which
keeps cross-core traffic low. The two indices live on separate 128-byte-aligned cache lines to avoid false sharing.

## Assumptions and limitations

Threading:

- Exactly one thread may call the producer operations and exactly one thread may call the consumer operations. The two
  roles may be the same thread. Calling producer operations from two threads, or consumer operations from two
  threads, is a data race.
- No operation blocks. When the queue is full or empty, the caller decides whether to spin, yield or back off.
- Both threads must have stopped using the queue, and synchronized with the destroying thread (for example with
  `std::thread::join`), before the queue is destroyed. The destructor destroys any items still in the queue.
- `size()` and `empty()` are snapshots that may already be stale when they return. On the producer thread the result
  may overcount and on the consumer thread it may undercount; on any other thread it can be any value from 0 to
  `capacity()`. Use the results of `try_push`, `front` and `try_pop` for decisions, not `size()`.

Capacity and memory:

- The capacity is chosen at construction and cannot change afterwards. The constructor throws
  `std::invalid_argument` for a capacity of 0 and `std::length_error` for `std::numeric_limits<std::size_t>::max()`.
- The constructor allocates `capacity + 1` slots of `sizeof(T)` on the heap and may throw `std::bad_alloc`. Push and
  pop never allocate.
- The queue object is aligned to 128 bytes and is neither copyable nor movable. Wrap it in `std::unique_ptr` if it has
  to be moved or shared.

Element type and errors:

- `T` must be a non-array object type with a non-throwing destructor; this is checked at compile time.
- `try_pop()` is only available when `T` has a non-throwing move constructor. For other types use `front()` and `pop()`.
- If constructing an item throws inside `try_push` or `try_emplace`, the exception propagates and the queue is
  unchanged. A push that returns `false` does not touch its argument, so a moved-from value can be retried.
- The pointer returned by `front()` stays valid until that item is popped or the queue is destroyed. Later pushes do
  not invalidate it.
- Calling `pop()` on an empty queue is undefined behavior. Debug builds catch it with an `assert`; builds with
  `NDEBUG` do not.

Progress guarantees:

- Queue operations are lock-free only if `std::atomic<std::size_t>::is_always_lock_free` is true for the target,
  which holds on mainstream 64-bit platforms; the header does not check it.
- An operation is only as non-blocking as `T`'s constructors, move operations and destructor, which may allocate or
  lock even when `noexcept`.

## Building

Requirements:

- Linux with GCC or Clang and C++20 support (tested with GCC 13 and Clang 18).
- CMake 3.24 or newer.
- Network access on the first configure of tests or benchmarks, which downloads GoogleTest 1.18.0. The library itself
  has no dependencies.

| CMake option            | Default                                       | Effect                                                             |
|-------------------------|-----------------------------------------------|--------------------------------------------------------------------|
| `SPSC_BUILD_TESTS`      | `ON` when spsc is the top-level CMake project | Builds `spsc_tests` and registers its tests with CTest.            |
| `SPSC_BUILD_BENCHMARKS` | `OFF`                                         | Builds `spsc_benchmarks`, which CTest does not run.                |
| `SPSC_SANITIZERS`       | empty                                         | Sanitizers for `spsc_tests`, e.g. `address;undefined` or `thread`. |

To use the queue from another CMake project, where tests and benchmarks are off by default:

```cmake
add_subdirectory(path/to/spsc)   # or FetchContent_Declare(spsc ...) and FetchContent_MakeAvailable(spsc)
target_link_libraries(your_target PRIVATE spsc::spsc)
```

Without CMake, add include/ to the include path and compile with -std=c++20.

## Running the tests

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

The binary can also be run directly, for example `./build/spsc_tests --gtest_filter='QueueStressTest.*'`.

| File                        | Covers                                                                                                          |
|-----------------------------|-----------------------------------------------------------------------------------------------------------------|
| `tests/spsc_test.cpp`       | Single-threaded behavior: capacity, ordering, wraparound, ``noexcept``, exceptions, randomized vs `std::deque`. |
| `tests/lifetime_test.cpp`   | Every item is constructed and destroyed exactly once, including by the queue's destructor.                      |
| `tests/stress_test.cpp`     | A producer and a consumer thread running concurrently, using every consumer operation.                          |

Sanitizer builds. ThreadSanitizer cannot be combined with AddressSanitizer, so they need separate build directories:

```sh
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug "-DSPSC_SANITIZERS=address;undefined"
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure

cmake -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DSPSC_SANITIZERS=thread
cmake --build build-tsan
ctest --test-dir build-tsan --output-on-failure
```

Older ThreadSanitizer runtimes abort with "unexpected memory mapping" on kernels with high `vm.mmap_rnd_bits`, so
CTest runs the ThreadSanitizer build with address-space randomization disabled through `setarch -R` when it is
available. Do the same when running that binary by hand: `setarch $(uname -m) -R ./build-tsan/spsc_tests`.

## Benchmarks

The benchmarks in `tests/benchmark_test.cpp` are GoogleTest tests that print timings instead of asserting them. They
need an optimized build:

```sh
cmake -B build-bench -DCMAKE_BUILD_TYPE=Release -DSPSC_BUILD_TESTS=OFF -DSPSC_BUILD_BENCHMARKS=ON
cmake --build build-bench
./build-bench/spsc_benchmarks
```

- Throughput: the producer streams 2^24 ints to the consumer, timed from the first push until the consumer has
  received the last item.
- RoundTrip: one int is sent to an echo thread and back, 2^20 times per run.
- Each runs at capacity 1024, where the producer often finds the queue full, and 2^24, where it never does, and
  consumes once with `front()` + `pop()` and once with `try_pop()`.
- Each benchmark does one warm-up run and five timed runs, then prints the median, minimum and maximum.