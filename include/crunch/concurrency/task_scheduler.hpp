// Copyright (c) 2011, Christian Rorvik
// Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

#ifndef CRUNCH_CONCURRENCY_TASK_SCHEDULER_HPP
#define CRUNCH_CONCURRENCY_TASK_SCHEDULER_HPP

#include "crunch/base/align.hpp"
#include "crunch/base/noncopyable.hpp"
#include "crunch/base/novtable.hpp"
#include "crunch/base/override.hpp"
#include "crunch/concurrency/future.hpp"
#include "crunch/concurrency/scheduler.hpp"
#include "crunch/concurrency/semaphore.hpp"
#include "crunch/concurrency/tasks_api.hpp"
#include "crunch/concurrency/thread_local.hpp"
#include "crunch/concurrency/versioned_data.hpp"
#include "crunch/concurrency/waitable.hpp"
#include "crunch/concurrency/work_stealing_queue.hpp"
#include "crunch/concurrency/detail/task_result.hpp"
#include "crunch/concurrency/detail/scheduled_task.hpp"
#include "crunch/concurrency/detail/scheduled_task_execution_context.hpp"

// TODO: remove
#include "crunch/concurrency/detail/system_mutex.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <vector>
#include <type_traits>

namespace Crunch { namespace Concurrency {

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
        auto Add (F f) -> Future<typename Detail::ResultOfTask<F>::Type>
        {
            return Add(f, nullptr, 0);
        }

        template<typename F>
        auto Add (F f, IWaitable** dependencies, std::uint32_t dependencyCount) -> Future<typename Detail::ResultOfTask<F>::Type>
        {
            typedef Future<typename Detail::ResultOfTask<F>::Type> FutureType;
            typedef typename FutureType::DataType FutureDataType;
            typedef typename FutureType::DataPtr FutureDataPtr;

            FutureDataType* futureData = new FutureDataType(2);
            Detail::ScheduledTask<F>* task = new Detail::ScheduledTask<F>(mOwner, std::move(f), futureData, dependencyCount);

            std::uint32_t addedCount = 0;
            for (std::uint32_t i = 0; i < dependencyCount; ++i)
                if (dependencies[i]->AddWaiter([=] { task->NotifyDependencyReady(); }))
                    addedCount++;

            const std::uint32_t readyCount = dependencyCount - addedCount;

            if (addedCount == 0 ||
                (readyCount > 0 && (task->mBarrierCount.Sub(readyCount) == readyCount)))
            {
                mTasks.Push(task);
            }
            
            return FutureType(FutureDataPtr(futureData, false));
        }


        virtual bool CanReEnter() CRUNCH_OVERRIDE { return false; }
        virtual State Run(IThrottler& throttler) CRUNCH_OVERRIDE;
        virtual IWaitable& GetHasWorkCondition() CRUNCH_OVERRIDE;

    private:
        typedef WorkStealingQueue<Detail::ScheduledTaskBase> WorkStealingTaskQueue;

        friend class TaskScheduler;

        TaskScheduler& mOwner;
        WorkStealingTaskQueue mTasks;
        std::uint32_t mContextsVersion;
        std::uint32_t const mMaxStealAttemptsBeforeIdle;
        std::uint32_t mStealAttemptCount;
        std::vector<std::shared_ptr<Context>> mNeighbors;

        std::vector<Detail::ScheduledTaskBase*> mRunLog; // Log of tasks run, for debugging
    };

    CRUNCH_CONCURRENCY_TASKS_API TaskScheduler();

    template<typename F>
    auto Add(F f) -> Future<typename Detail::ResultOfTask<F>::Type>
    {
        Context* context = GetContextInternal();
        if (context)
            return context->Add(f);
        else
            return mSharedContext.Add(f);
    }

    template<typename F>
    auto Add(F f, IWaitable** dependencies, std::uint32_t dependencyCount) -> Future<typename Detail::ResultOfTask<F>::Type>
    {
        Context* context = GetContextInternal();
        if (context)
            return context->Add(f, dependencies, dependencyCount);
        else
            return mSharedContext.Add(f, dependencies, dependencyCount);
    }

    CRUNCH_CONCURRENCY_TASKS_API void Enter();
    CRUNCH_CONCURRENCY_TASKS_API void Leave();

    CRUNCH_CONCURRENCY_TASKS_API virtual ISchedulerContext& GetContext() CRUNCH_OVERRIDE;
    virtual bool CanOrphan() CRUNCH_OVERRIDE { return true; }

private:
    friend class Detail::ScheduledTaskBase;

    CRUNCH_CONCURRENCY_TASKS_API static Context* GetContextInternal();
    CRUNCH_CONCURRENCY_TASKS_API void AddTask(Detail::ScheduledTaskBase* task);

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

CRUNCH_CONCURRENCY_TASKS_API extern TaskScheduler* gDefaultTaskScheduler;

#if !defined (VPM_SHARED_LIBS_BUILD)
inline TaskScheduler::Context* TaskScheduler::GetContextInternal()
{
    return tContext;
}
#endif

inline void TaskScheduler::AddTask(Detail::ScheduledTaskBase* task)
{
    Context* context = GetContextInternal();
    if (context && &context->mOwner == this)
        context->mTasks.Push(task);
    else
        mSharedContext.mTasks.Push(task);
}

}}

#endif

