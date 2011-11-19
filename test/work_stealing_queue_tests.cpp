// Copyright (c) 2011, Christian Rorvik
// Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

#include "crunch/concurrency/work_stealing_queue.hpp"
#include "crunch/test/framework.hpp"

namespace Crunch { namespace Concurrency {

BOOST_AUTO_TEST_SUITE(WorkStealingQueueTests)

BOOST_AUTO_TEST_CASE(Test)
{
    WorkStealingQueue<int> queue;

    int values[10];
    queue.Push(values);
    BOOST_CHECK_EQUAL(queue.Pop(), values);
    BOOST_CHECK_EQUAL(queue.Pop(), (int*)0);


    queue.Push(values);
    queue.Push(values + 1);
    BOOST_CHECK_EQUAL(queue.Pop(), values + 1);
    BOOST_CHECK_EQUAL(queue.Pop(), values);


    queue.Push(values);
    queue.Push(values + 1);
    BOOST_CHECK_EQUAL(queue.Steal(), values);
    BOOST_CHECK_EQUAL(queue.Steal(), values + 1);
    BOOST_CHECK_EQUAL(queue.Steal(), (int*)0);
}

BOOST_AUTO_TEST_SUITE_END()

}}
