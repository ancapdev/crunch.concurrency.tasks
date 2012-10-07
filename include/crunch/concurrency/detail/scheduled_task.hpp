// Copyright (c) 2012, Christian Rorvik
// Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

#ifndef CRUNCH_CONCURRENCY_DETAIL_SCHEDULED_TASK_HPP
#define CRUNCH_CONCURRENCY_DETAIL_SCHEDULED_TASK_HPP

#include "crunch/base/noncopyable.hpp"
#include "crunch/base/novtable.hpp"
#include "crunch/base/override.hpp"

#include "crunch/concurrency/atomic.hpp"
#include "crunch/concurrency/detail/task_result.hpp"

namespace Crunch { namespace Concurrency {

class TaskScheduler;

}}

namespace Crunch { namespace Concurrency { namespace Detail {

template<typename F> 
class ScheduledTaskExecutionContext;

class CRUNCH_NOVTABLE ScheduledTaskBase : NonCopyable
{
// protected: Some weirdness with access levels through lambdas on MSVC
public:
    friend class TaskScheduler;

    ScheduledTaskBase(TaskScheduler& owner, std::uint32_t barrierCount, std::uint32_t allocationSize)
        : mOwner(owner)
        , mBarrierCount(barrierCount, MEMORY_ORDER_RELEASE)
        , mAllocationSize(allocationSize)
    {}

    virtual void Dispatch() = 0;

    void NotifyDependencyReady()
    {
        if (1 == mBarrierCount.Decrement())
            Enque();
    }

    void Enque();

    TaskScheduler& mOwner;
    Atomic<std::uint32_t> mBarrierCount;
    std::uint32_t mAllocationSize;
};

template<typename F>
class ScheduledTask : public ScheduledTaskBase
{
public:
    typedef TaskTraits<F> Traits;
    typedef typename Traits::ReturnType ReturnType;
    typedef typename Traits::ResultType ResultType;
    typedef Future<ResultType> FutureType;
    typedef typename FutureType::DataType FutureDataType;
    typedef typename FutureType::DataPtr FutureDataPtr;

    // futureData must have 1 ref count already added
    ScheduledTask(TaskScheduler& owner, F&& f, FutureDataType* futureData, std::uint32_t barrierCount, std::uint32_t allocationSize = sizeof(ScheduledTask<F>))
        : ScheduledTaskBase(owner, barrierCount, allocationSize)
        , mFutureData(futureData) 
        , mFunctor(std::move(f))
    {
        CRUNCH_ASSERT(futureData->GetRefCount() > 0);
    }

    virtual void Dispatch() CRUNCH_OVERRIDE
    {
        Dispatch(typename Traits::ResultClass(), typename Traits::CallClass());
    }

private:
    friend class ScheduledTaskExecutionContext<F>;

    void Dispatch(TaskResultClassGeneric, TaskCallClassVoid)
    {
        mFutureData->Set(mFunctor());
        Release(mFutureData);
        delete this;
    }

    void Dispatch(TaskResultClassVoid, TaskCallClassVoid)
    {
        mFunctor();
        mFutureData->Set();
        Release(mFutureData);
        delete this;
    }

    void Dispatch(TaskResultClassVoid, TaskCallClassExecutionContext);

    void Dispatch(TaskResultClassFuture, TaskCallClassVoid);

    void Dispatch(TaskResultClassFuture, TaskCallClassExecutionContext);

    // typedef typename std::aligned_storage<sizeof(F), std::alignment_of<F>::value>::type FunctorStorageType;

    FutureDataType* mFutureData;
    F mFunctor;
    // FunctorStorageType mFunctorStorage;
};

template<typename F>
void ScheduledTask<F>::Dispatch(TaskResultClassFuture, TaskCallClassVoid)
{
    auto result = mFunctor();

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

}}}

#endif

