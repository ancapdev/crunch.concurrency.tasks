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
    : mSharedContext(*this)
{}

#if defined (CRUNCH_COMPILER_MSVC)
#   pragma warning (pop)
#endif

void TaskScheduler::Enter()
{
    CRUNCH_ASSERT_ALWAYS(tContext == nullptr);
    tContext = new Context(*this);

    mContexts.Update([] (ContextList& contexts)
    {
        contexts.push_back(std::shared_ptr<Context>(tContext));
    });
}

void TaskScheduler::Leave()
{
    CRUNCH_ASSERT_ALWAYS(tContext != nullptr);
    tContext->mNeighbors.clear(); // TODO: move to Context::Cleanup()
    mContexts.Update([] (ContextList& contexts)
    {
        Context* context = tContext;
        contexts.erase(std::find_if(contexts.begin(), contexts.end(), [context] (std::shared_ptr<Context> const& p) { return p.get() == context; }));
    });

    tContext = nullptr;
}

ISchedulerContext& TaskScheduler::GetContext()
{
    CRUNCH_ASSERT(tContext != nullptr);
    return *tContext;
}

TaskScheduler::Context::Context(TaskScheduler& owner)
    : mOwner(owner)
    , mContextsVersion(0)
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

        mOwner.mContexts.ReadIfDifferent(mContextsVersion, [this] (ContextList const& contexts)
        {
            mNeighbors.clear();
            Context* _this = this; // Work around MSVC nested capture bug
            std::copy_if(contexts.begin(), contexts.end(), std::back_inserter(mNeighbors), [_this] (std::shared_ptr<Context> const& p) { return p.get() != _this; });
        });

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
