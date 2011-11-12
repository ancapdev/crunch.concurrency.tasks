// Copyright (c) 2011, Christian Rorvik
// Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

#include "crunch/concurrency/meta_scheduler.hpp"
#include "crunch/concurrency/task_scheduler.hpp"
#include "crunch/concurrency/thread.hpp"
#include "crunch/containers/small_vector.hpp"

#include <boost/test/test_tools.hpp>
#include <boost/test/unit_test_suite.hpp>

#include <iostream>

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

    return s.Add([]{}, &dep[0], static_cast<uint32>(dep.size()));
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
    MetaScheduler::Configuration configuration;
    MetaScheduler metaScheduler(configuration);
    MetaScheduler::Context& metaSchedulerContext = metaScheduler.AcquireContext();

    TaskScheduler scheduler;
    Event shutdownEvent;
    Event workerDoneEvent;

    Thread t([&] {
        scheduler.Enter();
        scheduler.RunUntil(shutdownEvent);
        scheduler.Run(); // Orphaning of tasks not implemented, so at this point some tasks may be left.. run to finish all tasks
        scheduler.Leave();
        workerDoneEvent.Set();
    });

    scheduler.Enter();

    // int values[100] = {0,};
    Detail::SystemMutex mutex;
    auto const mainThreadId = GetThreadId();
    Future<void> work = ParallelFor(scheduler, IndexRange(0, 10000), [&](IndexRange r){
        Detail::SystemMutex::ScopedLock lock(mutex);
        if (GetThreadId() == mainThreadId)
            std::cout << r.begin << ":" << r.end << std::endl;
        else
            std::cout << "        " << r.begin << ":" << r.end << std::endl;

        /*
        std::for_each(r.begin, r.end, [](int& x){
            x = reinterpret_cast<int>(&x);
        });
        */
    });
    scheduler.Run();

    scheduler.Leave();
    shutdownEvent.Set();
    WaitFor(workerDoneEvent);

    metaSchedulerContext.Release();
}

BOOST_AUTO_TEST_SUITE_END()

}}
