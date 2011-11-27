// Copyright (c) 2011, Christian Rorvik
// Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

#ifndef CRUNCH_CONCURRENCY_TASK_SCHEDULER_HPP
#define CRUNCH_CONCURRENCY_TASK_SCHEDULER_HPP

#include "crunch/base/align.hpp"
#include "crunch/base/noncopyable.hpp"
#include "crunch/base/novtable.hpp"
#include "crunch/base/override.hpp"
#include "crunch/base/result_of.hpp"
#include "crunch/concurrency/future.hpp"
#include "crunch/concurrency/scheduler.hpp"
#include "crunch/concurrency/semaphore.hpp"
#include "crunch/concurrency/thread_local.hpp"
#include "crunch/concurrency/versioned_data.hpp"
#include "crunch/concurrency/waitable.hpp"
#include "crunch/concurrency/work_stealing_queue.hpp"


// TODO: remove
#include "crunch/concurrency/detail/system_mutex.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <vector>
#include <type_traits>

namespace Crunch { namespace Concurrency {

namespace Detail
{
    template<typename ResultType>
    struct ResultOfTask_
    {
        typedef ResultType Type;
    };

    template<typename T>
    struct ResultOfTask_<Future<T>>
    {
        typedef T Type;
    };
}

template<typename F>
struct ResultOfTask
{
    typedef typename Detail::ResultOfTask_<typename ResultOf<F>::Type>::Type Type;
};

/*
template<typename F, typename A0 = void, typename A1 = void>
struct ResultOfTask
{
    static F f;
    typedef typename Detail::ResultOfTask_<decltype(f(*(A0*)0, *(A1*)0))>::Type Type;
};

template<typename F, typename A0>
struct ResultOfTask<F, A0, void>
{
    static F f;
    typedef typename Detail::ResultOfTask_<decltype(f(*(A0*)0))>::Type Type;
};

template<typename F>
struct ResultOfTask<F, void, void>
{
    static F f;
    typedef typename Detail::ResultOfTask_<decltype(f())>::Type Type;
};
*/

class TaskScheduler;

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

    void NotifyDependencyReady();

    TaskScheduler& mOwner;
    Atomic<std::uint32_t> mBarrierCount;
    std::uint32_t mAllocationSize;
};

struct ResultClassVoid {};
struct ResultClassGeneric {};
struct ResultClassFuture {};

template<typename ResultType, typename ReturnType>
struct ResultClass
{
    typedef ResultClassGeneric Type;
};

template<>
struct ResultClass<void, void>
{
    typedef ResultClassVoid Type;
};

template<typename R>
struct ResultClass<R, Future<R>>
{
    typedef ResultClassFuture Type;
};

template<typename F>
class ScheduledTask : public ScheduledTaskBase
{
public:
    typedef typename ResultOf<F>::Type ReturnType;
    typedef typename ResultOfTask<F>::Type ResultType;
    typedef Future<ResultType> FutureType;
    typedef typename FutureType::DataType FutureDataType;
    typedef typename FutureType::DataPtr FutureDataPtr;

    // futureData must have 1 ref count already added
    ScheduledTask(TaskScheduler& owner, F&& f, FutureDataType* futureData, std::uint32_t barrierCount, std::uint32_t allocationSize = sizeof(ScheduledTask<F>))
        : ScheduledTaskBase(owner, barrierCount, allocationSize)
        , mFutureData(futureData) 
        , mFunctor(std::move(f))
    {}

    virtual void Dispatch() CRUNCH_OVERRIDE
    {
        Dispatch(typename ResultClass<ResultType, ReturnType>::Type());
    }

private:
    void Dispatch(ResultClassGeneric)
    {
        auto result = mFunctor();
        mFutureData->Set(result);
        Release(mFutureData);
        delete this;
    }

    void Dispatch(ResultClassVoid)
    {
        mFunctor();
        mFutureData->Set();
        Release(mFutureData);
        delete this;
    }

    void Dispatch(ResultClassFuture);

    // typedef typename std::aligned_storage<sizeof(F), std::alignment_of<F>::value>::type FunctorStorageType;

    FutureDataType* mFutureData;
    F mFunctor;
    // FunctorStorageType mFunctorStorage;
};

/*
// For use when the continuation doesn't fit in the original ScheduledTasks allocation
template<typename F, typename R>
class ContinuationImpl : public ScheduledTask
{
    static void Dispatch(ScheduledTask* ScheduledTask)
    {
        // Must delete the ScheduledTask because it's not part of the FutureData allocation
    }

    typedef typename std::aligned_storage<sizeof(F), std::alignment_of<F>::value>::type FunctorStorageType;

    Detail::FutureData<R>* mFutureData;
    FunctorStorageType mFunctorStorage;
};
*/

// TODO: store continuation size hint thread local per F type 
//       would enable over-allocation on initial task to avoid further allocations for continuations
//       only necessary when return type is void or Future<T>, i.e., continuable
class TaskScheduler : IScheduler, NonCopyable
{
public:
    // TODO: On destruction, orphan tasks
    class Context : ISchedulerContext, NonCopyable
    {
    public:
        Context(TaskScheduler& owner);

        template<typename F>
        auto Add (F f) -> Future<typename ResultOfTask<F>::Type>
        {
            return Add(f, nullptr, 0);
        }

        template<typename F>
        auto Add (F f, IWaitable** dependencies, std::uint32_t dependencyCount) -> Future<typename ResultOfTask<F>::Type>
        {
            typedef Future<typename ResultOfTask<F>::Type> FutureType;
            typedef typename FutureType::DataType FutureDataType;
            typedef typename FutureType::DataPtr FutureDataPtr;

            FutureDataType* futureData = new FutureDataType(2);
            ScheduledTask<F>* task = new ScheduledTask<F>(mOwner, std::move(f), futureData, dependencyCount);

            if (dependencyCount > 0)
            {
                std::uint32_t addedCount = 0;
                for (std::uint32_t i = 0; i < dependencyCount; ++i)
                    if (dependencies[i]->AddWaiter([=] { task->NotifyDependencyReady(); }))
                        addedCount++;

                if (addedCount < dependencyCount)
                {
                    if (addedCount == 0)
                        mTasks.Push(task);
                    else
                        if (task->mBarrierCount.Sub(dependencyCount - addedCount) == 1)
                            mTasks.Push(task);
                }
            }
            else
            {
                mTasks.Push(task);
            }

            return FutureType(FutureDataPtr(futureData, false));
        }

        virtual State Run(IThrottler const& throttler) CRUNCH_OVERRIDE;
        virtual IWaitable& GetHasWorkCondition() CRUNCH_OVERRIDE;

    private:
        typedef WorkStealingQueue<ScheduledTaskBase> WorkStealingTaskQueue;

        friend class TaskScheduler;

        TaskScheduler& mOwner;
        WorkStealingTaskQueue mTasks;
        std::uint32_t mContextsVersion;
        std::uint32_t const mMaxStealAttemptsBeforeIdle;
        std::uint32_t mStealAttemptCount;
        std::vector<std::shared_ptr<Context>> mNeighbors;

        std::vector<ScheduledTaskBase*> mRunLog; // Log of tasks run, for debugging
    };

    TaskScheduler();

    template<typename F>
    auto Add (F f) -> Future<typename ResultOfTask<F>::Type>
    {
        if (tContext)
            return tContext->Add(f);
        else
            return mSharedContext.Add(f);
    }

    template<typename F>
    auto Add (F f, IWaitable** dependencies, std::uint32_t dependencyCount) -> Future<typename ResultOfTask<F>::Type>
    {
        if (tContext)
            return tContext->Add(f, dependencies, dependencyCount);
        else
            return mSharedContext.Add(f, dependencies, dependencyCount);
    }

    void Enter();
    void Leave();

    virtual ISchedulerContext& GetContext() CRUNCH_OVERRIDE;

private:
    friend class ScheduledTaskBase;

    void AddTask(ScheduledTaskBase* task);


    // Contexts cache configuration locally and poll mConfigurationVersion for changes
    /*
    Detail::SystemMutex mConfigurationMutex;
    volatile int mConfigurationVersion;
    std::vector<std::shared_ptr<Context>> mContexts;
    */
    typedef std::vector<std::shared_ptr<Context>> ContextList;
    VersionedData<ContextList> mContexts;

    // Number of idle contexts
    Atomic<std::uint32_t> mIdleCount;
    Semaphore mWorkAvailable;

    // TODO: Need to lock around shared context
    // TODO: Might be better of with a more specialized queue instead of a shared context
    Context mSharedContext;

    static CRUNCH_THREAD_LOCAL Context* tContext;
};

extern TaskScheduler* gDefaultTaskScheduler;

inline void TaskScheduler::AddTask(ScheduledTaskBase* task)
{
    if (tContext && &tContext->mOwner == this)
        tContext->mTasks.Push(task);
    else
        mSharedContext.mTasks.Push(task);
}

inline void ScheduledTaskBase::NotifyDependencyReady()
{
    if (1 == mBarrierCount.Decrement())
        mOwner.AddTask(this);
}

template<typename F>
void ScheduledTask<F>::Dispatch(ResultClassFuture)
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

}}

#endif

