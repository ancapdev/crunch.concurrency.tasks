// Copyright (c) 2011, Christian Rorvik
// Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

#include "crunch/concurrency/work_stealing_queue.hpp"

#include "crunch/benchmarking/stopwatch.hpp"
#include "crunch/benchmarking/statistical_profiler.hpp"
#include "crunch/benchmarking/result_table.hpp"

#include "crunch/test/framework.hpp"

namespace Crunch { namespace Concurrency {

BOOST_AUTO_TEST_SUITE(WorkStealingQueueBenchmarks)

BOOST_AUTO_TEST_CASE(UncontendedBenchmark)
{
    using namespace Benchmarking;

    WorkStealingQueue<int> queue(2);

    int const reps = 100000;

    Stopwatch stopwatch;

    StatisticalProfiler profiler(0.01, 100, 1000, 10);
    queue.Push(nullptr);
    while (!profiler.IsDone())
    {
        stopwatch.Start();
        for (int i = 0; i < reps; ++i)
        {
            queue.Push(reinterpret_cast<int*>(i));
            queue.Pop();
        }
        stopwatch.Stop();

        profiler.AddSample(stopwatch.GetElapsedNanoseconds() / reps);
    }

    ResultTable<std::tuple<double, double, double, double, double>> results(
        "Concurrency.WorkStealingQueue.Uncontended",
        1,
        std::make_tuple("min", "max", "mean", "median", "stddev"));

    results.Add(std::make_tuple(
        profiler.GetMin(),
        profiler.GetMax(),
        profiler.GetMean(),
        profiler.GetMedian(),
        profiler.GetStdDev()));
}

BOOST_AUTO_TEST_SUITE_END()

}}
