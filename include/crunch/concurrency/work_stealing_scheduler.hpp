// Copyright (c) 2012, Christian Rorvik
// Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

#ifndef CRUNCH_CONCURRENCY_WORK_STEALING_SCHEDULER_HPP
#define CRUNCH_CONCURRENCY_WORK_STEALING_SCHEDULER_HPP

#include "crunch/base/noncopyable.hpp"
#include "crunch/base/novtable.hpp"
#include "crunch/base/override.hpp"
#include "crunch/concurrency/mpmc_lifo_queue.hpp"
#include "crunch/concurrency/semaphore.hpp"
#include "crunch/concurrency/scheduler.hpp"
#include "crunch/concurrency/tasks_api.hpp"
#include "crunch/concurrency/versioned_data.hpp"
#include "crunch/concurrency/work_stealing_queue.hpp"


#include <memory>
#include <vector>

namespace Crunch { namespace Concurrency {

// TODO: more state
// - affinity (might be better given as parameter when adding, will affect which queue the work goes in)
// - priority (might be better given as parameter when adding, will affect which queue the work goes in)
// - scheduling hints: working set size, fpu/int unit usage, long/short run
//   - might also affect target queue, so no need for state in work item itself
// - meta data for debug: name, dependency chain
//   - doesn't need to persist across Add call. If diagnostics are on, can be logged and mapped to WorkItem pointer
//
// If none of the extra state needs to persist for the lifetime of the work item,
// then better to leave the work item small and as a simple interface
//
/// Work item that can be scheduled in WorkStealingScheduler
struct CRUNCH_NOVTABLE IWorkItem
{
    // TODO: more run info
    // - calling context
    // - stolen flag
    virtual void Run() = 0;

protected:
    /// No virtual destructor as lifetime is not managed through IWorkItem
    ~IWorkItem() {}
};

// TODO: scheduling optimizations
// - Consider shared resources (e.g., Intel hyperthreading or AMD modules)
//   - Try to schedule integer and floating point work on separate threads that share those resources
//   - Try to span work items with large working sets across different cache units
//   - Try to schedule high priority items on threads not sharing any resources (item might be on longest serial chain)
// - Consider data locality
//   - Try to schedule repeated work items on the same cache units
//   - Try to steal from topologically close workers
//     - For high distance (e.g., NUMA), perhaps best guided by explicit hints or partitioning
// 
// Possible solutions:
//   context has
//   - queue for regular work (no hints)
//   - queue for vector work and/or large working set items (vector processed likely to be large number crunching)
//   - before run, check what adjacent context (shared L1 cache) is currently running, and select opposite queue
//     - bulldozer doesn't have shared L1, so coordinating with neighbour is potentially expensive
//     - if doing this, then must at least avoid writing current state unless different from last, and keep line in shared read mode
//     - a static bias might work better for this setup
//   - on steal, apply same logic
///
/// Work stealing scheduler
/// Multiple schedulers can exist, but only one scheduler can be active per thread at any given time.
/// I.e., a scheduler cannot be entered into while another scheduler is running
class WorkStealingScheduler : public IScheduler, NonCopyable
{
public:
    class Context : public ISchedulerContext, NonCopyable
    {
    public:
        Context(WorkStealingScheduler& owner);

        void Add(IWorkItem* work);

        //
        // ISchedulerContext
        //
        virtual bool CanReEnter() CRUNCH_OVERRIDE { return false; }
        virtual State Run(IThrottler& throttler) CRUNCH_OVERRIDE;
        virtual IWaitable& GetHasWorkCondition() CRUNCH_OVERRIDE;

    private:
        friend class WorkStealingScheduler;

        typedef WorkStealingQueue<IWorkItem> WorkQueue;

        WorkStealingScheduler& mOwner;
        WorkQueue mWork;
    };

    CRUNCH_CONCURRENCY_TASKS_API WorkStealingScheduler();

    CRUNCH_CONCURRENCY_TASKS_API void Enter();
    CRUNCH_CONCURRENCY_TASKS_API void Leave();

    CRUNCH_CONCURRENCY_TASKS_API void Add(IWorkItem* work);

    //
    // IScheduler
    //
    CRUNCH_CONCURRENCY_TASKS_API virtual ISchedulerContext& GetContext() CRUNCH_OVERRIDE;
    virtual bool CanOrphan() CRUNCH_OVERRIDE { return true; }

private:
    friend class Context;

    typedef std::vector<std::shared_ptr<Context>> ContextList;

    typedef MPMCLifoQueue<IWorkItem*> SharedWorkQueue;

    CRUNCH_CONCURRENCY_TASKS_API static Context* GetContextInternal();

    VersionedData<ContextList> mContexts;
    
    SharedWorkQueue mWork;

    // Track idle count for idle consensus and wake up
    Atomic<std::uint32_t> mIdleCount;
    Semaphore mWorkAvailable;

    static CRUNCH_THREAD_LOCAL Context* tContext;
};

#if !defined (VPM_SHARED_LIBS_BUILD)
inline WorkStealingScheduler::Context* WorkStealingScheduler::GetContextInternal()
{
    return tContext;
}
#endif

inline void WorkStealingScheduler::Add(IWorkItem* work)
{
    Context* context = GetContextInternal();
    if (context && &context->mOwner == this)
        context->Add(work);
    else
        // TODO: Notify workers of available work (probably by version variable)
        mWork.Push(work);
}

}}

#endif
