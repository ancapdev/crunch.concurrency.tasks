// Copyright (c) 2011, Christian Rorvik
// Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

#ifndef CRUNCH_CONCURRENCY_INDEX_RANGE_HPP
#define CRUNCH_CONCURRENCY_INDEX_RANGE_HPP

#include <utility>

namespace Crunch { namespace Concurrency {

template<typename IndexType>
class IndexRange
{
public:
    typedef IndexRange<IndexType> ThisType;

    IndexRange(IndexType begin, IndexType end, std::size_t grainSize = 1)
        : mBegin(begin)
        , mEnd(end)
        , mGrainSize(grainSize)
    {}

    bool IsSplittable() const
    {
        return Size() > mGrainSize;
    }

    std::pair<ThisType, ThisType> Split() const
    {
        IndexType const half = mBegin + (mEnd - mBegin) / 2;
        return std::make_pair(ThisType(mBegin, half, mGrainSize), ThisType(half, mEnd, mGrainSize));
    }

    std::size_t Size() const
    {
        return static_cast<std::size_t>(mEnd - mBegin);
    }

    std::size_t GrainSize() const
    {
        return mGrainSize;
    }

    IndexType Begin() const { return mBegin; }
    IndexType End() const { return mEnd; }

private:
    IndexType mBegin;
    IndexType mEnd;
    std::size_t mGrainSize;
};

template<typename IndexType>
IndexRange<IndexType> MakeIndexRange(IndexType begin, IndexType end, std::size_t grainSize = 1)
{
    return IndexRange<IndexType>(begin, end, grainSize);
}

}}

#endif
