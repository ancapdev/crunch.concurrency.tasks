// Copyright (c) 2012, Christian Rorvik
// Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

#include "crunch/concurrency/work_stealing_scheduler.hpp"

namespace Crunch { namespace Concurrency {

#if defined (VPM_SHARED_LIBS_BUILD)
WorkStealingScheduler::Context* WorkStealingScheduler::GetContextInternal()
{
    return tContext;
}
#endif

void WorkStealingScheduler::Context::Add(IWorkItem* work)
{
    // TODO: wake up context if idle
    mWork.Push(work);
}

}}
