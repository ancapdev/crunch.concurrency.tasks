// Copyright (c) 2011, Christian Rorvik
// Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

#ifndef CRUNCH_CONCURRENCY_TASK_SCHEDULER_HPP
#define CRUNCH_CONCURRENCY_TASK_SCHEDULER_HPP

#include "crunch/base/stdint.hpp"
#include "crunch/base/noncopyable.hpp"
#include "crunch/concurrency/future.hpp"
#include "crunch/concurrency/task.hpp"
#include "crunch/concurrency/thread_local.hpp"

// TODO: remove
#include "crunch/concurrency/detail/system_mutex.hpp"

#include <deque>
#include <functional>
#include <memory>
#include <vector>

namespace Crunch { namespace Concurrency {


class WorkStealingTaskQueue
{
public:
    void PushBack(TaskBase* task);
    TaskBase* PopBack();
    TaskBase* StealFront();

private:
    // TODO: Implement proper WS queue
    Detail::SystemMutex mMutex;
    std::deque<TaskBase*> mTasks;
};

// TODO: store continuation size hint thread local per F type 
//       would enable over-allocation on initial task to avoid further allocations for continuations
//       only necessary when return type is void or Future<T>, i.e., continuable
class TaskScheduler : NonCopyable
{
public:
    // TODO: On destruction, orphan tasks
    class Context : NonCopyable
    {
    public:
        Context(TaskScheduler& owner);

        template<typename F>
        auto Add (F f) -> typename Future<typename TaskTraits<F>::ResultType>
        {
            return Add(f, nullptr, 0);
        }

        template<typename F>
        auto Add (F f, IWaitable** dependencies, uint32 dependencyCount) -> Future<typename TaskTraits<F>::ResultType>
        {
            typedef Future<typename TaskTraits<F>::ResultType> FutureType;
            typedef typename FutureType::DataType FutureDataType;
            typedef typename FutureType::DataPtr FutureDataPtr;

            FutureDataType* futureData = new FutureDataType(2);
            Task<F>* task = new Task<F>(mOwner, std::move(f), futureData, dependencyCount);

            if (dependencyCount > 0)
            {
                for (uint32 i = 0; i < dependencyCount; ++i)
                    dependencies[i]->AddWaiter([=] { task->NotifyDependencyReady(); });
            }
            else
            {
                mTasks.PushBack(task);
            }

            return FutureType(FutureDataPtr(futureData, false));
        }

        void RunAll();

        void RunUntil(IWaitable& waitable);

    private:
        friend class TaskScheduler;

        TaskScheduler& mOwner;
        WorkStealingTaskQueue mTasks;
        int mConfigurationVersion;
        std::vector<std::shared_ptr<Context>> mNeighbours;
    };

    TaskScheduler();

    template<typename F>
    auto Add (F f) -> typename Future<typename TaskTraits<F>::ResultType>
    {
        if (tContext)
            return tContext->Add(f);
        else
            return mSharedContext.Add(f);
    }

    template<typename F>
    auto Add (F f, IWaitable** dependencies, uint32 dependencyCount) -> Future<typename TaskTraits<F>::ResultType>
    {
        if (tContext)
            return tContext->Add(f, dependencies, dependencyCount);
        else
            return mSharedContext.Add(f, dependencies, dependencyCount);
    }

    void Enter();
    void Leave();
    void Run();
    void RunUntil(IWaitable& waitable);

private:
    friend class TaskBase;

    void AddTask(TaskBase* task);


    // Contexts cache configuration locally and poll mConfigurationVersion for changes
    Detail::SystemMutex mConfigurationMutex;
    volatile int mConfigurationVersion;
    std::vector<std::shared_ptr<Context>> mContexts;

    // TODO: Need to lock around shared context
    // TODO: Might be better of with a more specialized queue instead of a shared context
    Context mSharedContext;

    static CRUNCH_THREAD_LOCAL Context* tContext;
};

inline void TaskScheduler::AddTask(TaskBase* task)
{
    if (tContext && &tContext->mOwner == this)
        tContext->mTasks.PushBack(task);
    else
        mSharedContext.mTasks.PushBack(task);
}

inline void TaskBase::NotifyDependencyReady()
{
    if (1 == mBarrierCount.Decrement())
        mOwner.AddTask(this);
}

template<typename F>
void Task<F>::Dispatch(ResultClassFuture)
{
    auto result = mFunctor();

    // Create continuation dependent on the completion of the returned Future
    auto futureData = mFutureData;
    uint32 const allocSize = mAllocationSize;
    TaskScheduler& owner = mOwner;

    // Get value from result
    auto contFunc = [=] () -> ResultType { return result.Get(); };
    typedef Task<decltype(contFunc)> ContTaskType;

    ContTaskType* contTask;
    // TODO: statically guarantee sufficient space for continuation in any task returning a Future<T>
    if (sizeof(Task<F>) >= sizeof(ContTaskType) || allocSize >= sizeof(ContTaskType))
    {
        // Reuse current allocation
        this->~Task<F>();
        contTask = new (this) ContTaskType(owner, std::move(contFunc), futureData, 1, allocSize);
    }
    else
    {
        // Create new allocation
        delete this;
        contTask = new ContTaskType(owner, std::move(contFunc), futureData, 1);
    }

    result.AddWaiter([=] { contTask->NotifyDependencyReady(); });
}

}}

#endif
