# Session 1 — questions asked

**Scope for this session:** covering only items 1–2 of the module's "Nail down" list (Google Benchmark harness, stopping the compiler from optimizing code away). Items 3–5 (warm-up, noise, pinning a thread) are deferred to a later session.

3. First explain to me CMakeLists.txt line by line.

**Answer:**
- `cmake_minimum_required(VERSION 3.16)` — oldest CMake version this file assumes; fails loudly on older installs instead of breaking confusingly later. 3.16 floor because `FetchContent` needs it.
- `project(a2_1_benchmarking_basics CXX)` — names the project, tells CMake to find a C++ compiler.
- `set(CMAKE_CXX_STANDARD 17)` / `CMAKE_CXX_STANDARD_REQUIRED ON` — compile as C++17; fail if the compiler can't actually do C++17 rather than silently downgrading.
- `if(NOT CMAKE_BUILD_TYPE) set(CMAKE_BUILD_TYPE Release) endif()` — defaults to `Release` (optimized) if nobody set a build type. Never benchmark a `-O0` (Debug) build — it won't even trigger dead-code elimination, so it tells you nothing real.
- `include(FetchContent)` — loads the CMake module for pulling in external deps as source, built as part of your own build.
- `set(BENCHMARK_ENABLE_TESTING OFF ...)` / `BENCHMARK_ENABLE_INSTALL OFF ...` — Google Benchmark's own options, set before it's pulled in: skip its internal test suite (slow, unneeded) and skip registering install rules on your system. `CACHE BOOL "" FORCE` is boilerplate for "set this as a cache var, override anything already there."
- `FetchContent_Declare(googlebenchmark, GIT_REPOSITORY ..., GIT_TAG v1.9.1)` — registers *where* to get the dependency, pinned to a tag for reproducibility, without fetching yet.
- `FetchContent_MakeAvailable(googlebenchmark)` — actually clones it and runs its CMakeLists as a subdirectory of your project. After this, the `benchmark::benchmark` target exists.
- `add_executable(bench_basics bench_basics.cpp)` — defines the executable and its source file.
- `target_link_libraries(bench_basics benchmark::benchmark)` — links against Google Benchmark so `BENCHMARK(...)` etc. are available.

4. What are we going to cover in the actual package?

**Answer:** Mapped to the five "Nail down" items in the module notes:
1. **Google Benchmark itself (the harness)** — `BENCHMARK(fn)` macro, `benchmark::State&`, the `for (auto _ : state)` loop, `BENCHMARK_MAIN()`.
2. **Stopping the compiler from optimizing code away** — `benchmark::DoNotOptimize(value)` and `benchmark::ClobberMemory()`.
3. **Warm-up** — not a separate call; it's how the library's calibration iterations already work, plus `--benchmark_min_warmup_time` to control it explicitly.
4. **Noise** — `--benchmark_repetitions=N`, `--benchmark_min_time=Xs`, reading mean/median/stddev across repetitions instead of trusting one run.
5. **Pinning a thread** — not part of the package at all; an OS-level concern (`taskset`, `sched_setaffinity`) to stop the scheduler migrating the benchmark between cores mid-run.

Items 1–4 live inside Google Benchmark; item 5 is bolted on from outside it.

7. Let's explore this state thing a bit — what is this exactly, are the number of loops set in the SomeWork function, or does the benchmark/main decide this?

**Answer:** Neither your function nor the `BENCHMARK(...)` macro decides the iteration count — the library's runner (kicked off by `BENCHMARK_MAIN()`) does, at runtime, by trial and error:
- Your function has no opinion; `for (auto _ : state)` just obeys whatever `state` says.
- The runner calls your function repeatedly, each time targeting a different iteration count: start small (e.g. 1), measure elapsed time, extrapolate how many iterations would take ~0.5s (`--benchmark_min_time` default), call again with that new target. Repeats until a run's elapsed time lands in the target window — that run's timing is what gets reported.
- So your function may be invoked multiple times before you see a result, each time with a fresh `state` carrying a different target.
- `BENCHMARK(fn)` only registers the function in a global list; it plays no role in choosing counts.

8. How to get the number of iterations in the function that the library is calling, from the state variable?

**Answer:** `state.iterations()` — called *after* the `for (auto _ : state)` loop finishes, not during it. Returns the total iterations that invocation actually ran. Typically paired with `state.SetItemsProcessed(state.iterations() * items_per_iteration)` to report throughput.

9. (pasted `bench_basics` run output) — what does each of these mean?

**Answer:**
- `Run on (18 X 4500 MHz CPUs)` / cache sizes / load average — machine info for context; load average low = system wasn't busy during the run.
- `***WARNING*** CPU scaling is enabled...` — the CPU frequency governor can change clock speed mid-run, a real noise source (fixable with `cpupower frequency-set --governor performance`).
- The `iterations 1`, `10`, `100`, ... lines — not built-in Google Benchmark output; came from a `std::cout` print added in the benchmark function itself, one per calibration call. The clean 10x growth breaks down after `1000000000` because the escalating target overflows a 32-bit integer somewhere in the growth math, producing wraparound garbage (`1410065408`, `-727379968`, etc.) — a downstream symptom, not the root cause.
- Final row `testing  0.000 ns  0.000 ns  1000000000000` — `Time`/`CPU` both ~0 while `Iterations` balloons to 10^12 is the classic sign the compiler proved the loop body does nothing observable and deleted it; the calibration step then runs away forever trying to find a count that takes measurable time.

10. (pasted own code snippet — the `++count` loop with `std::cout` after it)

**Answer:** The loop body (`++count;`) has no per-iteration observable effect, so the compiler can replace the entire loop with a closed-form computation (`count = N`) — a classic **induction variable elimination**. Printing `count` with `std::cout` *after* the loop doesn't prevent this: the compiler only needs to preserve the final value, not the act of looping, so it can compute `count = N` directly and skip running the loop physically. That's why `Time`/`CPU` read ~0 regardless of iteration count — there's no real loop left to time.

11. Where to place this DoNotOptimize tag?

**Answer:** Inside the loop, on every iteration — right after the work, not after the loop ends:
```cpp
for (auto _ : state) {
    ++count;
    benchmark::DoNotOptimize(count);
}
```
`DoNotOptimize` forces the compiler to treat the value as having escaped (observable), as if written somewhere it can't see into. Calling it once *after* the loop only forces the final computed value to escape — the compiler can still collapse the loop to one assignment and hand that single result to `DoNotOptimize`. Calling it *inside* the loop forces N individually-observable values, which blocks the collapse and forces the loop to actually run N times.

12. Tell me more about the state variable that is passed as the input.

**Answer:** `benchmark::State` is a per-run handle created by the runner and passed by reference into your function each call — never constructed by you.
- Loop control: `for (auto _ : state)`, `state.iterations()` (after the loop), `state.max_iterations` (public field, the target count for this invocation).
- Excluding non-timed work: `state.PauseTiming()` / `state.ResumeTiming()` — pause during setup that shouldn't count toward the measurement, resume before the real work.
- Extra reporting: `state.SetItemsProcessed(n)`, `state.SetBytesProcessed(n)` (throughput columns), `state.counters["name"] = value` (custom metric column).
- Parameterized benchmarks: `state.range(0)` reads back the input size when registered with `->Arg(...)` or `->Range(...)`.
- Multi-threaded benchmarks (`->Threads(N)`): `state.thread_index()`, `state.threads()`.
- Early exit: `state.SkipWithError("reason")`.
- For this module, only `for (auto _ : state)`, `state.iterations()`, and `DoNotOptimize` are actually needed — the rest (pause/resume, counters, range args, threads) are for later modules.

13. In question 12, I want more explanation on range and threads etc.

**Answer:**
- `range()` — lets one benchmark function produce multiple result rows at different input sizes instead of just one. Register with `->Arg(100)->Arg(1000)->Arg(10000)` (or `->Range(8, 8<<10)` for an auto-generated geometric sequence, step controlled by `->RangeMultiplier(N)`), then read the value back inside the function via `state.range(0)` (use `state.range(1)` etc. for a second dimension via `->Args({a, b})`). This is how you see how time scales with size, not just a single data point. Pairing with `state.SetComplexityN(n)` + `->Complexity()` lets Google Benchmark auto-fit your measured points to a complexity class (O(1), O(N), O(N log N), O(N²), ...).
- `Threads` — `->Threads(4)` doesn't run your function 4 times sequentially; it spins up 4 threads that run it *concurrently*, synchronized to start their timed region together (a barrier). This is for measuring things a single-threaded benchmark can't show: lock contention, false sharing, scaling with core count. Inside the function, `state.thread_index()` gives this call's 0-based thread id (useful to partition shared work so threads aren't all doing identical work), and `state.threads()` gives the total thread count for the registration. Things that should happen once per trial rather than once per thread (e.g. `SetItemsProcessed`) are typically guarded with `if (state.thread_index() == 0) { ... }`. You can register the same function at multiple thread counts (`->Threads(1)->Threads(2)->Threads(4)`) to see scaling directly. This is the natural lead-in to item 5 (pinning a thread) — once there are real concurrent threads, which physical core each one lands on starts to matter for noise.

14. (pasted `testing_BigO`/`testing_RMS` output) — what do these points mean?

**Answer:** These two rows only appear when a benchmark is registered with `->Complexity()` across multiple input sizes (via `->Arg()`/`->Range()` + `state.SetComplexityN(n)`).
- `testing_BigO     536924.05 N          1.31 N` — the fitted model, same column layout as `Time`/`CPU`. Left = fit for wall-clock Time, right = fit for CPU time. `N` means Google Benchmark picked O(N) (linear) as the best-matching class among its candidates (O(1), O(logN), O(N), O(NlogN), O(N²), O(N³)); the number is the fitted coefficient, so predicted time ≈ `coefficient * N`.
- `testing_RMS           90 %            53 %` — normalized root-mean-square error of that fit, as a percentage; **lower is better**. A good linear fit is usually single digits. `90%`/`53%` are both very poor fits — the data doesn't actually look linear, meaning something's off with the measurement (too few distinct sizes, noise, or a correctness bug like leftover dead-code elimination) rather than a real property of the algorithm.
- Red flag worth noting: the two coefficients (`536924.05` vs `1.31`) are wildly different for what should be the same underlying work (wall time vs CPU time) — another sign the measurement itself is unreliable here, not just the fit.
