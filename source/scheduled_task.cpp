// Copyright (c) 2012, Christian Rorvik
// Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

#include "crunch/concurrency/detail/scheduled_task.hpp"
#include "crunch/concurrency/task_scheduler.hpp"

namespace Crunch { namespace Concurrency { namespace Detail {

void ScheduledTaskBase::Enque()
{
    mOwner.AddTask(this);
}

}}}
