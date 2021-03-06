// Copyright (c) 2011, Christian Rorvik
// Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

#include "crunch/concurrency/index_range.hpp"
#include "crunch/concurrency/meta_scheduler.hpp"
#include "crunch/concurrency/parallel_for.hpp"
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

BOOST_AUTO_TEST_SUITE(TaskSchedulerTests)

BOOST_AUTO_TEST_CASE(ContinuationTest)
{
    MetaScheduler::Config config;
    MetaScheduler metaScheduler(config);
    MetaScheduler::Context& metaSchedulerContext = metaScheduler.AcquireContext();
    TaskScheduler scheduler;
    // TODO: pass flag to TaskScheduler constructor to say if it should become the global default scheduler
    gDefaultTaskScheduler = &scheduler;
    scheduler.Enter();

    NullThrottler throttler;

    volatile bool done = false;

    Thread t([&] {
        scheduler.Enter();
        while (!done)
            scheduler.GetContext().Run(throttler);
        scheduler.Leave();
    });

    Future<int> f = scheduler.Add([&] () -> Future<int> {
        std::cout << "initial" << std::endl;

        auto child1 = scheduler.Add([] () -> int { std::cout << "child1" << std::endl; return 1; });
        auto child2 = scheduler.Add([] () -> int { std::cout << "child2" << std::endl; return 2; });

        IWaitable* deps[2] = { &child1, &child2 };

        return scheduler.Add([=] () -> int {
            std::cout << "continuation " << child1.Get() << ", " << child2.Get() << std::endl;
            return child1.Get() + child2.Get();
        }, deps, 2);
    });

    WaitFor(f);
    std::cout << "Result: " << f.Get() << std::endl;

    auto f2 = scheduler.Add([&] (TaskExecutionContext<int>& context) -> Future<int> {
        std::cout << "In added" << std::endl;
        
        return context.ExtendWith([] () -> int {
            std::cout << "In continuation" << std::endl;
            return 123;
        });
    });
    WaitFor(f2);
    std::cout << "Result: " << f2.Get() << std::endl;


    auto f3 = scheduler.Add([&] (TaskExecutionContext<void>& context) {
        std::cout << "In added 2" << std::endl;
        
        context.ExtendWith([] () {
            std::cout << "In continuation 2" << std::endl;
        });
    });
    WaitFor(f3);

    done = true;
    t.Join();

    scheduler.Leave();
    metaSchedulerContext.Release();
}

#if 0
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
    auto r = MakeIndexRange(0, 1000);
    Future<void> work = ParallelFor(scheduler, r, [&](IndexRange<int>){
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
#endif

BOOST_AUTO_TEST_SUITE_END()

}}
