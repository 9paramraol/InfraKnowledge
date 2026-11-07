#include <benchmark/benchmark.h>
#include <iostream>

static void testing (benchmark::State& state)
{
    state.PauseTiming();
    std::vector<int> testingVec (state.range(0));
    state.ResumeTiming();

    int64_t count = 0;
    for (auto _ : state)
    {
        ++count;
        for (auto& elem: testingVec)
            elem = 0;
        benchmark::DoNotOptimize(count);
    }
    state.SetItemsProcessed(testingVec.size());
    state.SetComplexityN(testingVec.size());
    std::cout << "iterations " << count << " vecSize " << testingVec.size() << std::endl;
}

BENCHMARK(testing)->Range(8, 8 << 20)->Complexity();

BENCHMARK_MAIN();