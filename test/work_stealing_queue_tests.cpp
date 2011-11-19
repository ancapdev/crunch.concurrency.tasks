// Copyright (c) 2011, Christian Rorvik
// Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

#include "crunch/concurrency/work_stealing_queue.hpp"
#include "crunch/test/framework.hpp"

namespace Crunch { namespace Concurrency {

BOOST_AUTO_TEST_SUITE(WorkStealingQueueTests)

BOOST_AUTO_TEST_CASE(Test)
{
    WorkStealingQueue<int> queue;

}

BOOST_AUTO_TEST_SUITE_END()

}}
