// Copyright (c) 2011, Christian Rorvik
// Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

#include "crunch/concurrency/task_scheduler.hpp"

namespace Crunch { namespace Concurrency {
 
TaskScheduler* gDefaultTaskScheduler = nullptr;

CRUNCH_THREAD_LOCAL TaskScheduler::Context* TaskScheduler::tContext = nullptr;

#if defined (CRUNCH_COMPILER_MSVC)
#   pragma warning (push)
#   pragma warning (disable : 4355) // 'this' used in base member initializer list
#endif

TaskScheduler::TaskScheduler()
    : mConfigurationVersion(0)
    , mSharedContext(*this)
{}

#if defined (CRUNCH_COMPILER_MSVC)
#   pragma warning (pop)
#endif

void TaskScheduler::Enter()
{
    CRUNCH_ASSERT_ALWAYS(tContext == nullptr);
    tContext = new Context(*this);
    Detail::SystemMutex::ScopedLock lock(mConfigurationMutex);
    mContexts.push_back(std::shared_ptr<Context>(tContext));
    mConfigurationVersion++;
}

void TaskScheduler::Leave()
{
    CRUNCH_ASSERT_ALWAYS(tContext != nullptr);
    {
        Detail::SystemMutex::ScopedLock lock(mConfigurationMutex);
        tContext->mNeighbors.clear(); // TODO: move to Context::Cleanup()
        mContexts.erase(std::find_if(mContexts.begin(), mContexts.end(), [] (std::shared_ptr<Context> const& p) { return p.get() == tContext; }));
        mConfigurationVersion++;
    }

    tContext = nullptr;
}

ISchedulerContext& TaskScheduler::GetContext()
{
    CRUNCH_ASSERT(tContext != nullptr);
    return *tContext;
}

TaskScheduler::Context::Context(TaskScheduler& owner)
    : mOwner(owner)
    , mConfigurationVersion(0)
    , mMaxStealAttemptsBeforeIdle(20)
    , mStealAttemptCount(0)
{}

// TODO: exception safe dispatch (could be a per task flag, with try/catch in task dispatch implementation)
ISchedulerContext::State TaskScheduler::Context::Run(IThrottler const& throttler)
{
    for (;;)
    {
        // If we are not in stealing mode, run local tasks
        if (mStealAttemptCount == 0)
        {
            for (;;)
            {
                if (throttler.ShouldYield())
                    return State::Working;

                if (ScheduledTaskBase* task = mTasks.Pop())
                {
                    task->Dispatch();
                    mRunLog.push_back(task);
                }
                else
                    break;
            }
        }

        //
        // No more local tasks. Attempt stealing
        // 

        // Update neighbor config if it has changed
        if (mConfigurationVersion != mOwner.mConfigurationVersion)
        {
            mNeighbors.clear();
            Detail::SystemMutex::ScopedLock lock(mOwner.mConfigurationMutex);
            std::copy_if(mOwner.mContexts.begin(), mOwner.mContexts.end(), std::back_inserter(mNeighbors), [&] (std::shared_ptr<Context> const& p) { return p.get() != this; });
        }

        // If nowhere to steal from, return idle
        if (mNeighbors.empty())
            return State::Idle;

        // Select neighbor and steal
        // TODO: fast random number generator
        // TODO: steal local first
        int stealIndex = rand() % mNeighbors.size();
        if (ScheduledTaskBase* task = mNeighbors[stealIndex]->mTasks.Steal())
        {
            mStealAttemptCount = 0;
            task->Dispatch();
            mRunLog.push_back(task);
        }
        else
        {
            if (++mStealAttemptCount > mMaxStealAttemptsBeforeIdle)
            {
                // Meta scheduler will not call back in until mOwner.mWorkAvailable is posted.
                // It will only be posted if mOwner.mIdleCount > 0
                // mOwner.mIdleCount is incremented at the same time as mWorkAvailable is posted, so no need to change on entry to Run
                mOwner.mIdleCount.Increment();
                return State::Idle;
            }
            else
            {
                return State::Polling;
            }
        }
    }
}

IWaitable& TaskScheduler::Context::GetHasWorkCondition()
{
    return mOwner.mWorkAvailable;
}

}}
