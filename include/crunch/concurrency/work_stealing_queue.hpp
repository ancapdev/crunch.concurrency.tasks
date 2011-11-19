// Copyright (c) 2011, Christian Rorvik
// Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

#ifndef CRUNCH_CONCURRENCY_WORK_STEALING_QUEUE_HPP
#define CRUNCH_CONCURRENCY_WORK_STEALING_QUEUE_HPP

#include "crunch/base/align.hpp"
#include "crunch/base/noncopyable.hpp"
#include "crunch/base/memory.hpp"
#include "crunch/base/stdint.hpp"
#include "crunch/concurrency/atomic.hpp"
#include "crunch/concurrency/mpmc_lifo_list.hpp"

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
        static CircularArray* Create(uint32 logSize)
        {
            return new (sAllocator.Allocate(logSize)) CircularArray(logSize);
        }

        void Destroy()
        {
            sAllocator.Free(this, mLogSize);
        }

        CircularArray* Grow(int64 front, int64 back)
        {
            CircularArray* newArray = new (sAllocator.Allocate(mLogSize + 1)) CircularArray(this);

            for (int64 i = front; i < back; ++i)
                newArray->Set(i, Get(i));

            return newArray;
        }

        CircularArray* Shrink(int64 front, int64 back)
        {
            CRUNCH_ASSERT(CanShrink());
            CRUNCH_ASSERT((back - front) < GetSize() / 2);

            // TODO: Set watermark and only copy parts that have changed since grow

            CircularArray* newArray = mParent;
            for (int64 i = front; i < back; ++i)
                newArray->Set(i, Get(i));

            return newArray;
        }

        bool CanShrink() const
        {
            return mParent != nullptr;
        }

        void Set(int64 index, T* value)
        {
            mElements[index & mSizeMinusOne] = value;
        }

        T* Get(int64 index) const
        {
            return mElements[index & mSizeMinusOne];
        }

        int64 GetSize() const
        {
            return mSizeMinusOne + 1;
        }

        int64 GetSizeMinusOne() const
        {
            return mSizeMinusOne;
        }

    private:
        CircularArray(uint32 logSize)
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
        uint32 mLogSize;
        int64 mSizeMinusOne;
        T* mElements[1]; // Actually dynamically sized

        struct CRUNCH_ALIGN_PREFIX(128) Allocator
        {
            typedef Detail::WorkStealingQueueFreeListNode Node;

            ~Allocator()
            {
                for (uint32 i = 0; i < MaxLogSize; ++i)
                    while (Node* node = mFreeLists[i].Pop())
                        FreeAligned(node);
            }

            void* Allocate(uint32 logSize)
            {
                CRUNCH_ASSERT(logSize <= MaxLogSize);

                if (Node* node = mFreeLists[logSize].Pop())
                    return node;

                // TODO: grow free list in batches
                return MallocAligned((std::size_t(1) << logSize) + sizeof(CircularArray), 128);
            }

            void Free(void* buffer, uint32 logSize)
            {
                CRUNCH_ASSERT(logSize <= MaxLogSize);

                mFreeLists[logSize].Push(reinterpret_cast<Node*>(buffer));
            }

            // Support logSize <= 32
            static uint32 const MaxLogSize = 32;

            // Align to cacheline size. 64 should be sufficient on newer x86, but other archs often have larger line sizes
            // Some levels might also use larger lines
            typedef CRUNCH_ALIGN_PREFIX(128) MPMCLifoList<Node> CRUNCH_ALIGN_POSTFIX(128) FreeList;

            FreeList mFreeLists[MaxLogSize];
        } CRUNCH_ALIGN_POSTFIX(128);

        static Allocator sAllocator;
    };

    WorkStealingQueue(uint32 initialLogSize = 6)
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
    void Push(T* value)
    {
        int64 const back = mBack.Load(MEMORY_ORDER_ACQUIRE);
        int64 const front = mFront.Load(MEMORY_ORDER_ACQUIRE);
        CircularArray* array = mArray.Load(MEMORY_ORDER_ACQUIRE);
        int64 const size = back - front;
        if (size >= array->GetSizeMinusOne())
        {
            array = array->Grow(front, back);
            mArray.Store(array, MEMORY_ORDER_RELEASE);
        }
        array->Set(back, value);
        mBack.Store(back + 1, MEMORY_ORDER_RELEASE);
    }

    // Fraction of spaced used to trigger shrink. Must be >= 3.
    static uint32 const ShrinkFraction = 3;

    T* Pop()
    {
        int64 back = mBack.Load(MEMORY_ORDER_ACQUIRE);
        CircularArray* array = mArray.Load(MEMORY_ORDER_ACQUIRE);
        back = back - 1;
        mBack.Store(back, MEMORY_ORDER_RELEASE);
        int64 front = mFront.Load(MEMORY_ORDER_ACQUIRE);
        int64 const size = back - front;
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
                int64 const newSize = newArray->GetSize();
                mBack.Store(back + newSize, MEMORY_ORDER_RELEASE);
                int64 front = mFront.Load(MEMORY_ORDER_ACQUIRE);
                if (!mFront.CompareAndSwap(front + newSize, front))
                    mBack.Store(back);

                array->Destroy();
            }
            return value;
        }
        if (!mFront.CompareAndSwap(front + 1, front))
            value = nullptr; // steal took last element
        mBack.Store(front + 1, MEMORY_ORDER_RELEASE);
        return value;
    }


    // TODO: differentiate empty and failed to steal?
    T* Steal()
    {
        int64 front = mFront.Load(MEMORY_ORDER_ACQUIRE);
        CircularArray* oldArray = mArray.Load(MEMORY_ORDER_ACQUIRE);
        int64 const back = mBack.Load(MEMORY_ORDER_ACQUIRE);
        CircularArray* array = mArray.Load(MEMORY_ORDER_ACQUIRE);
        int64 const size = back - front;
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

    Atomic<int64> mFront;
    Atomic<int64> mBack;
    Atomic<CircularArray*> mArray;
};

template<typename T>
typename WorkStealingQueue<T>::CircularArray::Allocator WorkStealingQueue<T>::CircularArray::sAllocator;

}}

#endif
