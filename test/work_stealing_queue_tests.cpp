// Copyright (c) 2011, Christian Rorvik
// Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

#include "crunch/concurrency/thread.hpp"
#include "crunch/concurrency/work_stealing_queue.hpp"
#include "crunch/concurrency/detail/system_event.hpp"
#include "crunch/test/framework.hpp"

namespace Crunch { namespace Concurrency {

BOOST_AUTO_TEST_SUITE(WorkStealingQueueTests)

BOOST_AUTO_TEST_CASE(BasicOperationsTest)
{
    WorkStealingQueue<int> queue;
    int values[1];

    // Push, Pop Succeed, Pop Fail
    queue.Push(values);
    BOOST_CHECK_EQUAL(queue.Pop(), values);
    BOOST_CHECK_EQUAL(queue.Pop(), (int*)0);

    // Push, Push, Pop Succeed, Pop Succeed
    queue.Push(values);
    queue.Push(values + 1);
    BOOST_CHECK_EQUAL(queue.Pop(), values + 1);
    BOOST_CHECK_EQUAL(queue.Pop(), values);

    // Push, Push, Steal Succeed, Steal Succceed, Steal Fail
    queue.Push(values);
    queue.Push(values + 1);
    BOOST_CHECK_EQUAL(queue.Steal(), values);
    BOOST_CHECK_EQUAL(queue.Steal(), values + 1);
    BOOST_CHECK_EQUAL(queue.Steal(), (int*)0);
}

BOOST_AUTO_TEST_CASE(StressTest)
{
    int const logSize = 4;
    int const pushCount = (1 << logSize) - 1;
    int const repeatCount = 100000;
    WorkStealingQueue<int> queue(logSize);
    Detail::SystemEvent workerStartedEvent;
    volatile bool done = false;
    volatile int stolen = 0;
    int popped = 0;

    Thread consumer([&]()
    {
        workerStartedEvent.Set();
        int* last = nullptr;
        while (!done)
        {
            int* x = queue.Steal();
            if (x != nullptr)
            {
                if (x <= last)
                    CRUNCH_HALT();

                stolen++;
                last = x;
            }
        }
    });

    workerStartedEvent.Wait();

    int* counter = nullptr;
    for (int i = 0; i < repeatCount; ++i)
    {
        for (int j = 0; j < pushCount; ++j)
        {
            queue.Push(++counter);
        }

        while (int* x = queue.Pop())
        {
            if (x > counter)
                CRUNCH_HALT();

            popped++;
        }
    }

    done = true;
    consumer.Join();

    BOOST_CHECK_EQUAL(stolen + popped, repeatCount * pushCount);
}

BOOST_AUTO_TEST_SUITE_END()

}}
