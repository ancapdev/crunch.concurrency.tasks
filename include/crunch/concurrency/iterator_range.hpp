// Copyright (c) 2011, Christian Rorvik
// Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

#ifndef CRUNCH_CONCURRENCY_ITERATOR_RANGE_HPP
#define CRUNCH_CONCURRENCY_ITEARTOR_RANGE_HPP

#include <iterator>
#include <utility>

namespace Crunch { namespace Concurrency {

template<typename IteratorType>
class IteratorRange
{
public:
    typedef IteratorRange<IteratorType> ThisType;

    IteratorRange(IteratorType begin, IteratorType end, std::size_t grainSize = 1)
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
        IteratorType const half = std::next(mBegin, Size() / 2);
        return std::make_pair(ThisType(mBegin, half, mGrainSize), ThisType(half, mEnd, mGrainSize));
    }

    std::size_t Size() const
    {
        return std::distance(mBegin, mEnd);
    }

    std::size_t GrainSize() const
    {
        return mGrainSize;
    }

    IteratorType Begin() const { return mBegin; }
    IteratorType End() const { return mEnd; }

private:
    IteratorType mBegin;
    IteratorType mEnd;
    std::size_t mGrainSize;
};

template<typename IteratorType>
IteratorRange<IteratorType> MakeIteratorRange(IteratorType begin, IteratorType end, std::size_t grainSize = 1)
{
    return IteratorRange<IteratorType>(begin, end, grainSize);
}

/*
class MultiRange
{
public:
    typedef MultiRange ThisType;

    bool IsSplittable() const
    {
        // Return any splittable
    }

    std::pair<ThisType, ThisType> Split() const
    {
        // Split sub range with multiple of grainsize size
    }

private:
};
*/

}}

#endif
