// Copyright (c) 2011, Christian Rorvik
// Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

#ifndef CRUNCH_CONCURRENCY_TASK_HPP
#define CRUNCH_CONCURRENCY_TASK_HPP

#include "crunch/base/align.hpp"
#include "crunch/base/noncopyable.hpp"
#include "crunch/base/override.hpp"
#include "crunch/base/stdint.hpp"

#include "crunch/concurrency/waitable.hpp"
#include "crunch/concurrency/future.hpp"

#include <type_traits>

namespace Crunch { namespace Concurrency {

class TaskBase : NonCopyable
{
// protected: Some weirdness with access levels through lambdas on MSVC
public:
    friend class TaskScheduler;

    TaskBase(TaskScheduler& owner, uint32 barrierCount, uint32 allocationSize)
        : mOwner(owner)
        , mBarrierCount(barrierCount, MEMORY_ORDER_RELEASE)
        , mAllocationSize(allocationSize)
    {}

    virtual void Dispatch() = 0;

    void NotifyDependencyReady();

    TaskScheduler& mOwner;
    Atomic<uint32> mBarrierCount;
    uint32 mAllocationSize;
};

template<typename R>
struct UnwrapResultType
{
    typedef R Type;
};

template<typename R>
struct UnwrapResultType<Future<R>>
{
    typedef R Type;
};

template<typename F>
struct TaskTraits
{
    // MSVC 10 std::result_of is broken
    typedef decltype((*(F*)(0))()) ReturnType;
    typedef typename UnwrapResultType<ReturnType>::Type ResultType;
};

struct ResultClassVoid {};
struct ResultClassGeneric {};
struct ResultClassFuture {};

template<typename ResultType, typename ReturnType>
struct ResultClass
{
    typedef ResultClassGeneric Type;
};

template<>
struct ResultClass<void, void>
{
    typedef ResultClassVoid Type;
};

template<typename R>
struct ResultClass<R, Future<R>>
{
    typedef ResultClassFuture Type;
};

template<typename F>
class Task : public TaskBase
{
public:
    typedef typename TaskTraits<F>::ReturnType ReturnType;
    typedef typename TaskTraits<F>::ResultType ResultType;
    typedef Future<ResultType> FutureType;
    typedef typename FutureType::DataType FutureDataType;
    typedef typename FutureType::DataPtr FutureDataPtr;

    // futureData must have 1 ref count already added
    Task(TaskScheduler& owner, F&& f, FutureDataType* futureData, uint32 barrierCount, uint32 allocationSize = sizeof(Task<F>))
        : TaskBase(owner, barrierCount, allocationSize)
        , mFutureData(futureData) 
        , mFunctor(std::move(f))
    {}

    virtual void Dispatch() CRUNCH_OVERRIDE
    {
        Dispatch(typename ResultClass<ResultType, ReturnType>::Type());
    }

private:
    void Dispatch(ResultClassGeneric)
    {
        auto result = mFunctor();
        mFutureData->Set(result);
        Release(mFutureData);
        delete this;
    }

    void Dispatch(ResultClassVoid)
    {
        mFunctor();
        mFutureData->Set();
        Release(mFutureData);
        delete this;
    }

    void Dispatch(ResultClassFuture);

    // typedef typename std::aligned_storage<sizeof(F), std::alignment_of<F>::value>::type FunctorStorageType;

    FutureDataType* mFutureData;
    F mFunctor;
    // FunctorStorageType mFunctorStorage;
};

/*
// For use when the continuation doesn't fit in the original tasks allocation
template<typename F, typename R>
class ContinuationImpl : public Task
{
    static void Dispatch(Task* task)
    {
        // Must delete the task because it's not part of the FutureData allocation
    }

    typedef typename std::aligned_storage<sizeof(F), std::alignment_of<F>::value>::type FunctorStorageType;

    Detail::FutureData<R>* mFutureData;
    FunctorStorageType mFunctorStorage;
};
*/

}}

#endif
