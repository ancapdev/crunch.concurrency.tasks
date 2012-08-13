// Copyright (c) 2011, Christian Rorvik
// Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

#ifndef CRUNCH_CONCURRENCY_PARALLEL_FOR_HPP
#define CRUNCH_CONCURRENCY_PARALLEL_FOR_HPP

#include "crunch/concurrency/range.hpp"
#include "crunch/concurrency/task_scheduler.hpp"
#include "crunch/containers/small_vector.hpp"

#include <algorithm>

namespace Crunch { namespace Concurrency {

// TODO: Check if current task was stolen and reduce split depth if not
template<typename R, typename F>
Future<void> ParallelFor(TaskScheduler& s, R const& r, F f)
{
    R rr  = r;
    Containers::SmallVector<Future<void>, 32> children;
    while (IsRangeSplittable(rr))
    {
        auto sr = SplitRange(rr);
        children.push_back(s.Add([=,&s] {
            return ParallelFor(s, sr.second, f);
        }));
        rr = sr.first;
    }

    f(rr);

    Containers::SmallVector<IWaitable*, 32> dep;
    std::for_each(children.begin(), children.end(), [&](Future<void>& f){
        dep.push_back(&f);
    });

    return s.Add([]{}, &dep[0], static_cast<std::uint32_t>(dep.size()));
}

}}

#endif