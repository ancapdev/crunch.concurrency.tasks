// Copyright (c) 2012, Christian Rorvik
// Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

#ifndef CRUNCH_CONCURRENCY_TASK_EXECUTION_CONTEXT_HPP
#define CRUNCH_CONCURRENCY_TASK_EXECUTION_CONTEXT_HPP

#include "crunch/base/noncopyable.hpp"

#include "crunch/concurrency/future.hpp"
#include "crunch/concurrency/detail/scheduled_task.hpp"

namespace Crunch { namespace Concurrency {

class TaskScheduler;

template<typename ResultType>
struct ExtendResultType
{
    typedef Future<ResultType> Type;
};

template<>
struct ExtendResultType<void>
{
    typedef void Type;
};

template<typename ResultType>
class CRUNCH_NOVTABLE TaskExecutionContext : NonCopyable
{
public:
    typedef Future<ResultType> FutureType;

    template<typename F>
    typename ExtendResultType<ResultType>::Type ExtendWith(F f)
    {
        return ExtendWith(f, nullptr, 0);
    }

    // NOTE: must be last call in task, with immediate return
    //       current task state is undefined after this call
    template<typename F>
    typename ExtendResultType<ResultType>::Type ExtendWith(F f, IWaitable** dependencies, std::uint32_t dependencyCount)
    {
        CRUNCH_ASSERT(!mHasContinuation);
        mHasContinuation = true;

        // TODO: Apart from allocation and future data re-use, this code is the same as scheduler context..
        void* allocation = AllocateContinuation(sizeof(Detail::ScheduledTask<F>));
        Detail::ScheduledTask<F>* task = new (allocation) Detail::ScheduledTask<F>(mOwner, std::move(f), mFutureData, dependencyCount);

        std::uint32_t addedCount = 0;
        for (std::uint32_t i = 0; i < dependencyCount; ++i)
            if (dependencies[i]->AddWaiter([=] { task->NotifyDependencyReady(); }))
                addedCount++;

        const std::uint32_t readyCount = dependencyCount - addedCount;

        if (addedCount == 0 ||
            (readyCount > 0 && (task->mBarrierCount.Sub(readyCount) == readyCount)))
        {
            task->Enque();
        }

        return MakeResult();
    }

    typename ExtendResultType<ResultType>::Type MakeResult()
    {
        // The future returned should be captured by ScheduledTask::Dispatch
        // and never used. The future data addref/decref is redundant
        // Perhaps the continuation API should be reserved for void functions
        // and less performance critical code can use return Add([]{}) with auto-continuation for convenience
        
        return FutureType(FutureDataPtr(mFutureData));
    }

    // TODO: protect and friend
    // protected:

    typedef typename FutureType::DataType FutureDataType;
    typedef typename FutureType::DataPtr FutureDataPtr;

    TaskExecutionContext(TaskScheduler& owner, FutureDataType* futureData)
        : mOwner(owner)
        , mHasContinuation(false)
        , mFutureData(futureData)
    {}

    virtual void* AllocateContinuation(std::size_t requiredSize) = 0;

    TaskScheduler& mOwner;
    bool mHasContinuation;
    FutureDataType* mFutureData;
};

template<>
inline void TaskExecutionContext<void>::MakeResult()
{}

}}

#endif
