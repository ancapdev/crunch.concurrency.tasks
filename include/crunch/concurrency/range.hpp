// Copyright (c) 2011, Christian Rorvik
// Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

#ifndef CRUNCH_CONCURRENCY_RANGE_HPP
#define CRUNCH_CONCURRENCY_RANGE_HPP

namespace Crunch { namespace Concurrency {

template<typename R>
bool IsRangeSplittable(const R& r)
{
    return r.IsSplittable();
}

template<typename R>
std::pair<R, R> SplitRange(const R& r)
{
    return r.Split();
}

}}

#endif
