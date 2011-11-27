// Copyright (c) 2011, Christian Rorvik
// Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

#ifndef CRUNCH_CONCURRENCY_WORK_STEALING_QUEUE_HPP
#define CRUNCH_CONCURRENCY_WORK_STEALING_QUEUE_HPP

#include "crunch/base/align.hpp"
#include "crunch/base/inline.hpp"
#include "crunch/base/noncopyable.hpp"
#include "crunch/base/memory.hpp"
#include "crunch/concurrency/atomic.hpp"
#include "crunch/concurrency/mpmc_lifo_list.hpp"

#include <cstdint>

namespace Crunch { namespace Concurrency {

// TODO: steal half work queues
// TODO: adaptive work stealing A-STEAL

namespace Detail
{
    struct WorkStealingQueueFreeListNode
    {
        WorkStealingQueueFreeListNode* next;
    };

    inline void SetNext(WorkStealingQueueFreeListNode& node, WorkStealingQueueFreeListNode* next)
    {
        node.next = next;
    }

    inline WorkStealingQueueFreeListNode* GetNext(WorkStealingQueueFreeListNode& node)
    {
        return node.next;
    }

}

// Implementation of Chase and Lev "Dynamic Circular Work-Stealing Deque"
// TODO: Make signed types unsigned (if negative binary & will not work for modulo)
template<typename T>
class WorkStealingQueue : NonCopyable
{
public:
    // TODO: clean up free list in static destructor
    // Circular array with buffer embedded. Custom allocation with no system reclamation.
    // When array grows, it links back to previous smaller sized array. Arrays are only released to free list on shrink.
    class CircularArray : NonCopyable
    {
    public:
        static CircularArray* Create(std::uint32_t logSize)
        {
            return new (sAllocator.Allocate(logSize)) CircularArray(logSize);
        }

        void Destroy()
        {
            sAllocator.Free(this, mLogSize);
        }

        CircularArray* Grow(std::int64_t front, std::int64_t back)
        {
            CircularArray* newArray = new (sAllocator.Allocate(mLogSize + 1)) CircularArray(this);

            for (std::int64_t i = front; i < back; ++i)
                newArray->Set(i, Get(i));

            return newArray;
        }

        CircularArray* Shrink(std::int64_t front, std::int64_t back)
        {
            CRUNCH_ASSERT(CanShrink());
            CRUNCH_ASSERT((back - front) < GetSize() / 2);

            // TODO: Set watermark and only copy parts that have changed since grow

            CircularArray* newArray = mParent;
            for (std::int64_t i = front; i < back; ++i)
                newArray->Set(i, Get(i));

            return newArray;
        }

        bool CanShrink() const
        {
            return mParent != nullptr;
        }

        void Set(std::int64_t index, T* value)
        {
            mElements[index & mSizeMinusOne] = value;
        }

        T* Get(std::int64_t index) const
        {
            return mElements[index & mSizeMinusOne];
        }

        std::int64_t GetSize() const
        {
            return mSizeMinusOne + 1;
        }

        std::int64_t GetSizeMinusOne() const
        {
            return mSizeMinusOne;
        }

    private:
        CircularArray(std::uint32_t logSize)
            : mParent(nullptr)
            , mLogSize(logSize)
            , mSizeMinusOne((1ll << logSize) - 1)
        {}

        CircularArray(CircularArray* parent)
            : mParent(parent)
            , mLogSize(parent->mLogSize + 1)
            , mSizeMinusOne((1ll << mLogSize) - 1)
        {}

        CircularArray* mParent;
        std::uint32_t mLogSize;
        std::int64_t mSizeMinusOne;
        T* mElements[1]; // Actually dynamically sized

        struct CRUNCH_ALIGN_PREFIX(128) Allocator
        {
            typedef Detail::WorkStealingQueueFreeListNode Node;

            ~Allocator()
            {
                for (std::uint32_t i = 0; i < MaxLogSize; ++i)
                    while (Node* node = mFreeLists[i].Pop())
                        FreeAligned(node);
            }

            void* Allocate(std::uint32_t logSize)
            {
                CRUNCH_ASSERT(logSize <= MaxLogSize);

                if (Node* node = mFreeLists[logSize].Pop())
                    return node;

                // TODO: grow free list in batches
                return MallocAligned((sizeof(T*) << logSize) + sizeof(CircularArray), 128);
            }

            void Free(void* buffer, std::uint32_t logSize)
            {
                CRUNCH_ASSERT(logSize <= MaxLogSize);

                mFreeLists[logSize].Push(reinterpret_cast<Node*>(buffer));
            }

            // Support logSize <= 32
            static std::uint32_t const MaxLogSize = 32;

            // Align to cacheline size. 64 should be sufficient on newer x86, but other archs often have larger line sizes
            // Some levels might also use larger lines
            typedef CRUNCH_ALIGN_PREFIX(128) MPMCLifoList<Node> CRUNCH_ALIGN_POSTFIX(128) FreeList;

            FreeList mFreeLists[MaxLogSize];
        } CRUNCH_ALIGN_POSTFIX(128);

        static Allocator sAllocator;
    };

    WorkStealingQueue(std::uint32_t initialLogSize = 6)
        : mFront(0)
        , mBack(0)
        , mArray(CircularArray::Create(initialLogSize))
    {}

    ~WorkStealingQueue()
    {
        // Unwind chain of arrays and destroy them all
        CircularArray* array = mArray;
        while (array->CanShrink())
            array = array->Shrink(0, 0);
        array->Destroy();
    }

    // TODO: avoid front access in Push. Cache conservative front
    CRUNCH_ALWAYS_INLINE void Push(T* value)
    {
        std::int64_t const back = mBack.Load(MEMORY_ORDER_ACQUIRE);
        std::int64_t const front = mFront.Load(MEMORY_ORDER_ACQUIRE);
        CircularArray* array = mArray.Load(MEMORY_ORDER_ACQUIRE);
        std::int64_t const size = back - front;
        if (size >= array->GetSizeMinusOne())
        {
            array = array->Grow(front, back);
            mArray.Store(array, MEMORY_ORDER_RELEASE);
        }
        array->Set(back, value);
        mBack.Store(back + 1, MEMORY_ORDER_RELEASE);
    }

    // Fraction of spaced used to trigger shrink. Must be >= 3.
    static std::uint32_t const ShrinkFraction = 3;

    CRUNCH_ALWAYS_INLINE T* Pop()
    {
        std::int64_t back = mBack.Load(MEMORY_ORDER_ACQUIRE);
        CircularArray* array = mArray.Load(MEMORY_ORDER_ACQUIRE);
        back = back - 1;
        mBack.Store(back, MEMORY_ORDER_SEQ_CST);
        std::int64_t front = mFront.Load(MEMORY_ORDER_SEQ_CST);
        std::int64_t const size = back - front;
        if (size < 0)
        {
            mBack.Store(front, MEMORY_ORDER_RELEASE);
            return nullptr; // empty
        }

        T* value = array->Get(back);
        if (size > 0)
        {
            // Try shrink
            if (array->CanShrink() && 
                size < array->GetSize() / ShrinkFraction)
            {
                CircularArray* newArray = array->Shrink(front, back);
                mArray.Store(newArray, MEMORY_ORDER_RELEASE);
                std::int64_t const newSize = newArray->GetSize();
                mBack.Store(back + newSize, MEMORY_ORDER_RELEASE);
                std::int64_t front = mFront.Load(MEMORY_ORDER_ACQUIRE);
                if (!mFront.CompareAndSwap(front + newSize, front))
                    mBack.Store(back);

                array->Destroy();
            }
            return value;
        }
        std::int64_t const newFront = front + 1;
        if (!mFront.CompareAndSwap(newFront, front))
            value = nullptr; // steal took last element
        mBack.Store(newFront, MEMORY_ORDER_RELEASE);
        return value;
    }


    // TODO: differentiate empty and failed to steal?
    T* Steal()
    {
        std::int64_t front = mFront.Load(MEMORY_ORDER_ACQUIRE);
        CircularArray* oldArray = mArray.Load(MEMORY_ORDER_ACQUIRE);
        std::int64_t const back = mBack.Load(MEMORY_ORDER_ACQUIRE);
        CircularArray* array = mArray.Load(MEMORY_ORDER_ACQUIRE);
        std::int64_t const size = back - front;
        if (size <= 0)
            return nullptr; // Empty
        if ((size & array->GetSizeMinusOne()) == 0)
        {
            if (array == oldArray && front == mFront.Load(MEMORY_ORDER_ACQUIRE))
                return nullptr; // Empty
            else
                return nullptr; // Abort
        }
        T* value = array->Get(front);
        if (!mFront.CompareAndSwap(front + 1, front))
            return nullptr; // Abort

        return value;
    }

private:
    void Grow();

    Atomic<std::int64_t> mFront;
    Atomic<std::int64_t> mBack;
    Atomic<CircularArray*> mArray;
};

template<typename T>
typename WorkStealingQueue<T>::CircularArray::Allocator WorkStealingQueue<T>::CircularArray::sAllocator;

}}

#endif
