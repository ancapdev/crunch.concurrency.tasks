// Copyright (c) 2011, Christian Rorvik
// Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

#include "crunch/concurrency/task_scheduler.hpp"

namespace Crunch { namespace Concurrency {

void WorkStealingTaskQueue::PushBack(TaskBase* task)
{
    Detail::SystemMutex::ScopedLock lock(mMutex);
    mTasks.push_back(task);
}

TaskBase* WorkStealingTaskQueue::PopBack()
{
    Detail::SystemMutex::ScopedLock lock(mMutex);

    if (mTasks.empty())
        return nullptr;

    TaskBase* task = mTasks.back();
    mTasks.pop_back();
    return task;
}

TaskBase* WorkStealingTaskQueue::StealFront()
{
    Detail::SystemMutex::ScopedLock lock(mMutex);

    if (mTasks.empty())
        return nullptr;

    TaskBase* task = mTasks.front();
    mTasks.pop_front();
    return task;
}


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
        mContexts.erase(std::find_if(mContexts.begin(), mContexts.end(), [] (std::shared_ptr<Context> const& p) { return p.get() == tContext; }));
        mConfigurationVersion++;
    }

    tContext = nullptr;
}

void TaskScheduler::Run()
{
    CRUNCH_ASSERT_ALWAYS(tContext != nullptr);
    tContext->RunAll();
}

void TaskScheduler::RunUntil(IWaitable& waitable)
{
    CRUNCH_ASSERT_ALWAYS(tContext != nullptr);
    tContext->RunUntil(waitable);
}
TaskScheduler::Context::Context(TaskScheduler& owner)
    : mOwner(owner)
    , mConfigurationVersion(0)
{}

void TaskScheduler::Context::RunAll()
{
    for (;;)
    {
        while (TaskBase* task = mTasks.PopBack())
            task->Dispatch();

        if (mConfigurationVersion != mOwner.mConfigurationVersion)
        {
            mNeighbours.clear();
            Detail::SystemMutex::ScopedLock lock(mOwner.mConfigurationMutex);
            std::copy_if(mOwner.mContexts.begin(), mOwner.mContexts.end(), std::back_inserter(mNeighbours), [&] (std::shared_ptr<Context> const& p) { return p.get() != this; });
        }

        bool stole = false;
        for (auto it = mNeighbours.begin(), end = mNeighbours.end(); it != end; ++it)
        {
            if (TaskBase* task = (*it)->mTasks.StealFront())
            {
                task->Dispatch();
                stole = true;
                break;
            }
        }

        if (!stole)
            return;
    }
}

void TaskScheduler::Context::RunUntil(IWaitable& waitable)
{
    volatile bool done = false;
    waitable.AddWaiter([&] { done = true; });
    while (!done)
    {
        while (TaskBase* task = mTasks.PopBack())
            task->Dispatch();

        if (mConfigurationVersion != mOwner.mConfigurationVersion)
        {
            mNeighbours.clear();
            Detail::SystemMutex::ScopedLock lock(mOwner.mConfigurationMutex);
            std::copy_if(mOwner.mContexts.begin(), mOwner.mContexts.end(), std::back_inserter(mNeighbours), [&] (std::shared_ptr<Context> const& p) { return p.get() != this; });
        }

        bool stole = false;
        for (auto it = mNeighbours.begin(), end = mNeighbours.end(); it != end; ++it)
        {
            if (TaskBase* task = (*it)->mTasks.StealFront())
            {
                task->Dispatch();
                stole = true;
                break;
            }
        }

        // if (!stole)
        //    return;
    }
}

}}
