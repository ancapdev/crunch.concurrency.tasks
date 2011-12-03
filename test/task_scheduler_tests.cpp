// Copyright (c) 2011, Christian Rorvik
// Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

#include "crunch/concurrency/meta_scheduler.hpp"
#include "crunch/concurrency/task.hpp"
#include "crunch/concurrency/task_scheduler.hpp"
#include "crunch/concurrency/thread.hpp"
#include "crunch/containers/small_vector.hpp"

#include <boost/test/test_tools.hpp>
#include <boost/test/unit_test_suite.hpp>

#include <iostream>
#include <tuple>
#include <vector>

namespace Crunch { namespace Concurrency {

template<typename R>
bool IsRangeSplittable(const R& r)
{
    return r.IsSplittable();
}

template<typename R>
std::pair<R, R> SplitRange(const R& r)
{
    return r.Split();
}

template<typename R, typename F>
Future<void> ParallelFor(TaskScheduler& s, R const& r, F f)
{
    R rr  = r;
    Containers::SmallVector<Future<void>, 32> children;
    while (IsRangeSplittable(rr))
    {
        auto sr = SplitRange(rr);
        children.push_back(s.Add([=,&s] {
            return ParallelFor(s, sr.second, f);
        }));
        rr = sr.first;
    }

    f(rr);

    Containers::SmallVector<IWaitable*, 32> dep;
    std::for_each(children.begin(), children.end(), [&](Future<void>& f){
        dep.push_back(&f);
    });

    return s.Add([]{}, &dep[0], static_cast<std::uint32_t>(dep.size()));
}

template<typename IteratorType>
struct MyRange
{
    typedef MyRange<IteratorType> ThisType;

    MyRange(IteratorType begin, IteratorType end)
        : begin(begin)
        , end(end)
    {}

    bool IsSplittable() const
    {
        return std::distance(begin, end) > 10;
    }

    std::pair<ThisType, ThisType> Split() const
    {
        auto half = std::distance(begin, end) / 2;
        return std::make_pair(ThisType(begin, begin + half), ThisType(begin + half, end));
    }

    IteratorType begin;
    IteratorType end;
};

struct IndexRange
{
    IndexRange(int begin, int end)
        : begin(begin)
        , end(end)
    {}

    bool IsSplittable() const
    {
        return (end - begin) > 10;
    }

    std::pair<IndexRange, IndexRange> Split() const
    {
        int half = (begin + end) / 2;
        return std::make_pair(IndexRange(begin, half), IndexRange(half, end));
    }

    int begin;
    int end;
};

template<typename IteratorType>
MyRange<IteratorType> MakeRange(IteratorType begin, IteratorType end)
{
    return MyRange<IteratorType>(begin, end);
}

BOOST_AUTO_TEST_SUITE(TaskSchedulerTests)

BOOST_AUTO_TEST_CASE(RemoveMe)
{
    for (int i = 0; i < 100; ++i)
    {


    MetaScheduler::Config config;
    MetaScheduler metaScheduler(config);
    MetaScheduler::Context& metaSchedulerContext = metaScheduler.AcquireContext();

    TaskScheduler scheduler;
    Event workerDoneEvent;

    struct NullThrottler : IThrottler
    {
        NullThrottler() {} // Keep GCC happy

        virtual bool ShouldYield() CRUNCH_OVERRIDE
        {
            return false;
        }
    };

    volatile bool done = false;
    NullThrottler throttler;

    Thread t([&] {
        //*
        scheduler.Enter();
        while (!done)
            scheduler.GetContext().Run(throttler);
        scheduler.Leave();
        //*/
        workerDoneEvent.Set();
    });

    scheduler.Enter();

    // int values[100] = {0,};
    Detail::SystemMutex mutex;
    auto const mainThreadId = GetThreadId();
    Future<void> work = ParallelFor(scheduler, IndexRange(0, 10000), [&](IndexRange){
        /*
        Detail::SystemMutex::ScopedLock lock(mutex);
        if (GetThreadId() == mainThreadId)
            std::cout << r.begin << ":" << r.end << std::endl;
        else
            std::cout << "        " << r.begin << ":" << r.end << std::endl;
        */

        /*
        std::for_each(r.begin, r.end, [](int& x){
            x = reinterpret_cast<int>(&x);
        });
        */
    });

    scheduler.GetContext().Run(throttler);
    done = true;

    scheduler.Leave();
    WaitFor(workerDoneEvent);

    metaSchedulerContext.Release();

    }
}

BOOST_AUTO_TEST_CASE(RemoveMe2)
{
    MetaScheduler::Config config;
    MetaScheduler metaScheduler(config);
    MetaScheduler::Context& metaSchedulerContext = metaScheduler.AcquireContext();
    TaskScheduler scheduler;
    // TODO: pass flag to TaskScheduler constructor to say if it should become the global default scheduler
    gDefaultTaskScheduler = &scheduler;
    scheduler.Enter();

    RunTask([] {
        std::cout << "In task" << std::endl;
    }).Then([] {
        std::cout << "In next task" << std::endl;
    });

    // TODO: Reserve operator overloading for a special wrapper object (TaskStream) to avoid confusion
    //   - Have operators for combining multiple results to one continuation. E.g., ','
    //   - Have operator to split same result to multiple continuations?
    //     - Maybe group of tasks can all take same input from their shared dependencies.
    /*
    // Example: Would create a Future<void> that prints 1, 2, 3
    TaskStream()
        >> ([] { return 1; }, [] { return 2 }, [] { return 3; })
        >> [] (int a, int b, int c) { std::cout << a << ", " << b << ", " << c << std::endl; };
    */


    auto a = RunTask([] { return 1; });
    auto b = a >> [] (int x) { return x * 2; };
    auto c = b >> [] (int x) { return x * x; };
    auto d = c >> [] (int x) { return x + 1; };
    auto e = d >> [] (int x) { std::cout << "Produced " << x << std::endl; };


    struct NullThrottler : IThrottler
    {
        NullThrottler() {} // Keep GCC happy

        virtual bool ShouldYield() CRUNCH_OVERRIDE
        {
            return false;
        }
    };

    NullThrottler throttler;
    scheduler.GetContext().Run(throttler);

    scheduler.Leave();
    metaSchedulerContext.Release();
}

BOOST_AUTO_TEST_SUITE_END()

}}
