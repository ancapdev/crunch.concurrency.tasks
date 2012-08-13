// Copyright (c) 2011, Christian Rorvik
// Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

#include "crunch/concurrency/index_range.hpp"
#include "crunch/concurrency/parallel_for.hpp"

#include <boost/test/test_tools.hpp>
#include <boost/test/unit_test_suite.hpp>

#include <algorithm>
#include <vector>

namespace Crunch { namespace Concurrency {

BOOST_AUTO_TEST_SUITE(ParallelForTests)

BOOST_AUTO_TEST_CASE(RunTest)
{
    TaskScheduler scheduler;
    scheduler.Enter();

    for (std::size_t size = 0; size <= 23; ++size)
    {
        for (std::size_t grainSize = 1; grainSize <= size; ++grainSize)
        {
            std::vector<int> runCount(100, 0);

            Future<void> result = ParallelFor(scheduler, MakeIndexRange(std::size_t(0), size, grainSize), [&](IndexRange<std::size_t> const& r) {
                BOOST_CHECK_LE(r.Size(), grainSize);
                BOOST_CHECK_GE(r.Size(), std::min(grainSize / 2, size));

                for (auto i = r.Begin(); i != r.End(); ++i)
                    runCount[i]++;
            });

            NullThrottler throttler;
            scheduler.GetContext().Run(throttler);

            BOOST_REQUIRE(result.IsReady());

            for (std::size_t i = 0; i < size; ++i)
                BOOST_CHECK_EQUAL(runCount[i], 1);
        }
    }

    scheduler.Leave();
}

BOOST_AUTO_TEST_SUITE_END()

}}
