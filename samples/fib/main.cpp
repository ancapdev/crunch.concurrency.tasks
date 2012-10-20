// Copyright (c) 2012, Christian Rorvik
// Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

#include "crunch/concurrency/meta_scheduler.hpp"
#include "crunch/concurrency/task_scheduler.hpp"
#include "crunch/concurrency/task.hpp"

namespace Crunch { namespace Concurrency {

int Fib(int x)
{
    if (x <= 2)
        return x;
    else
        return Fib(x - 1) + Fib(x - 2);
}

Future<int> ParFib(int x)
{
    if (x < 5)
    {
        return gDefaultTaskScheduler->Add([=] () -> int { return Fib(x); });
    }
    else
    {
        Future<int> c1 = gDefaultTaskScheduler->Add([=] () -> Future<int> { return ParFib(x - 1); });
        Future<int> c2 = gDefaultTaskScheduler->Add([=] () -> Future<int> { return ParFib(x - 2); });
        IWaitable* deps[] = { &c1, &c2 };
        return gDefaultTaskScheduler->Add(
            [=] () -> int { return c1.Get() + c2.Get(); },
            deps, 2);
    }
}

/*
Task<int> ParFib2(int x)
{
    if (x < 5)
    {
        return Task<int>([=] () -> int { Fib(x); });
    }
    else
    {
        Task<int> c1([=] () -> Task<int> { return ParFib2(x - 1); });
        Task<int> c2([=] () -> Task<int> { return ParFib2(x - 2); });
        return Join(c1, c2, [] (int v1, int v2) -> int { return v1 + v2; });
    }
}
*/

// TODO: use TaskResultOf to support F where F(_,_) -> Future<T>
template<typename T0, typename T1, typename F>
auto Join(
    Future<T0> f0,
    Future<T1> f1,
    F f,
    TaskScheduler& scheduler = *gDefaultTaskScheduler) -> Future<typename ResultOf<F(T0, T1)>::Type>
{
    IWaitable* deps[2] = { &f0, &f1 };
    return scheduler.Add([=] () -> typename ResultOf<F(T0, T1)>::Type { return f(f0.Get(), f1.Get()); }, deps, 2);
}

Future<int> ParFib2(int x)
{
    if (x < 5)
    {
        return gDefaultTaskScheduler->Add([=] () -> int { return Fib(x); });
    }
    else
    {
        Future<int> c1 = gDefaultTaskScheduler->Add([=] () -> Future<int> { return ParFib(x - 1); });
        Future<int> c2 = gDefaultTaskScheduler->Add([=] () -> Future<int> { return ParFib(x - 2); });
        return Join(c1, c2, [] (int v1, int v2) -> int { return v1 + v2; });
    }
}


void FibSample()
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

    Thread t1([&] {
        scheduler.Enter();
        while (!done)
            scheduler.GetContext().Run(throttler);
        scheduler.Leave();
    });

    Thread t2([&] {
        scheduler.Enter();
        while (!done)
            scheduler.GetContext().Run(throttler);
        scheduler.Leave();
    });

    int x = 20;
    Future<int> y = ParFib2(x);
    WaitFor(y);
    std::cout << "ParFib(" << x << ") = " << y.Get() << std::endl;
    std::cout << "Fib(" << x << ") = " << Fib(x) << std::endl;


    done = true;
    t1.Join();
    t2.Join();

    scheduler.Leave();
    metaSchedulerContext.Release();
}

}}

int main()
{
    Crunch::Concurrency::FibSample();
    return 0;
}
