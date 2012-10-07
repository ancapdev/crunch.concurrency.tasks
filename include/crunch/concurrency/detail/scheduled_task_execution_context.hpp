// Copyright (c) 2012, Christian Rorvik
// Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

#ifndef CRUNCH_CONCURRENCY_DETAIL_SCHEDULED_TASK_EXECUTION_CONTEXT_HPP
#define CRUNCH_CONCURRENCY_DETAIL_SCHEDULED_TASK_EXECUTION_CONTEXT_HPP

#include "crunch/concurrency/task_execution_context.hpp"
#include "crunch/concurrency/detail/scheduled_task.hpp"


// TODO: Remove
#include <iostream>

namespace Crunch { namespace Concurrency { namespace Detail {

template<typename F>
class ScheduledTaskExecutionContext : public TaskExecutionContext<typename ResultOfTask<F>::Type>
{
public:
    ScheduledTaskExecutionContext(ScheduledTask<F>* owner)
        : TaskExecutionContext<typename ResultOfTask<F>::Type>(owner->mOwner, owner->mFutureData)
        , mOwner(owner)
    {}

    virtual void* AllocateContinuation(std::size_t requiredSize) CRUNCH_OVERRIDE
    {
        // Cache allocation size before destroying object
        const std::size_t allocationSize = mOwner->mAllocationSize;

        // Re-use object memory if possible
        if (allocationSize >= requiredSize)
        {
            // TODO: Need to return size of allocation
            //       Or special constructor which doesn't overwrite previous fields...
            mOwner->~ScheduledTask<F>();
            return mOwner;
        }
        else
        {
            delete mOwner;
            return new char[requiredSize];
        }
    }

private:
    ScheduledTask<F>* mOwner;
};

template<typename F>
void ScheduledTask<F>::Dispatch(TaskResultClassVoid, TaskCallClassExecutionContext)
{
    ScheduledTaskExecutionContext<F> execContext(this);

    mFunctor(execContext);

    if (!execContext.mHasContinuation)
    {
        mFutureData->Set();
        Release(mFutureData);
        delete this;
    }
}

template<typename F>
void ScheduledTask<F>::Dispatch(TaskResultClassFuture, TaskCallClassExecutionContext)
{
    ScheduledTaskExecutionContext<F> execContext(this);

    auto result = mFunctor(execContext);

    if (!execContext.mHasContinuation)
    {
        // TODO: Refactor and share with other Dispatch method

        // Create continuation dependent on the completion of the returned Future
        auto futureData = mFutureData;
        std::uint32_t const allocSize = mAllocationSize;
        TaskScheduler& owner = mOwner;

        // Get value from result
        auto contFunc = [=] () -> ResultType { return result.Get(); };
        typedef ScheduledTask<decltype(contFunc)> ContTaskType;

        ContTaskType* contTask;
        // TODO: statically guarantee sufficient space for continuation in any task returning a Future<T>
        if (sizeof(ScheduledTask<F>) >= sizeof(ContTaskType) || allocSize >= sizeof(ContTaskType))
        {
            // Reuse current allocation
            this->~ScheduledTask<F>();
            contTask = new (this) ContTaskType(owner, std::move(contFunc), futureData, 1, allocSize);
        }
        else
        {
            // Create new allocation
            delete this;
            contTask = new ContTaskType(owner, std::move(contFunc), futureData, 1);
        }

        if (!result.AddWaiter([=] { contTask->NotifyDependencyReady(); }))
            contTask->NotifyDependencyReady();

    }
}

}}}

#endif

