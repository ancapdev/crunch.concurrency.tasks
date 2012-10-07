// Copyright (c) 2011, Christian Rorvik
// Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

#ifndef CRUNCH_CONCURRENCY_TASK_HPP
#define CRUNCH_CONCURRENCY_TASK_HPP

#include "crunch/concurrency/task_scheduler.hpp"

namespace Crunch { namespace Concurrency {

// High level task primitive
template<typename ResultType>
class Task
{
public:
    template<typename F>
    Task(F f, TaskScheduler& scheduler = *gDefaultTaskScheduler)
        : mScheduler(&scheduler)
        , mFuture(scheduler.Add(f))
    {}

    // TODO: make private
    Task(TaskScheduler* scheduler, Future<ResultType> future)
        : mScheduler(scheduler)
        , mFuture(future)
    {}

    Future<ResultType> const& GetFuture() const
    {
        return mFuture;
    }

    template<typename F>
    auto Then(F f) -> Task<typename Detail::ResultOfTask<F(ResultType)>::Type>
    {
        IWaitable* dep = &mFuture;
        Future<ResultType> result = mFuture;
        Future<typename Detail::ResultOfTask<F(ResultType)>::Type> thenFuture = mScheduler->Add([=]{ return f(result.Get()); }, &dep, 1);
        return Task<typename Detail::ResultOfTask<F(ResultType)>::Type>(mScheduler, thenFuture);
    }

private:
    TaskScheduler* mScheduler;
    Future<ResultType> mFuture;
};

template<>
class Task<void>
{
public:
    template<typename F>
    Task(F f, TaskScheduler& scheduler = *gDefaultTaskScheduler)
        : mScheduler(&scheduler)
        , mFuture(scheduler.Add(f))
    {}

    Task(TaskScheduler* scheduler, Future<void> future)
        : mScheduler(scheduler)
        , mFuture(future)
    {}

    Future<void> GetFuture() const
    {
        return mFuture;
    }

    template<typename F>
    auto Then(F f) -> Task<typename Detail::ResultOfTask<F>::Type>
    {
        IWaitable* dep = &mFuture;
        return Task<typename Detail::ResultOfTask<F>::Type>(mScheduler, mScheduler->Add(f, &dep, 1));
    }

private:
    TaskScheduler* mScheduler;
    Future<void> mFuture;
};

template<typename F>
auto RunTask(F f) -> Task<typename Detail::ResultOfTask<F>::Type>
{
    return Task<typename Detail::ResultOfTask<F>::Type>(f);
}

template<typename R, typename F>
auto operator >> (Task<R> t, F f) -> decltype(t.Then(f))
{
    return t.Then(f);
}

}}

#endif
