// Copyright (c) 2012, Christian Rorvik
// Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

#ifndef CRUNCH_CONCURRENCY_DETAIL_TASK_RESULT_HPP
#define CRUNCH_CONCURRENCY_DETAIL_TASK_RESULT_HPP

#include "crunch/base/result_of.hpp"
#include "crunch/concurrency/future.hpp"

namespace Crunch { namespace Concurrency { namespace Detail {

/*
template<typename ResultType>
struct ResultOfTask_
{
    typedef ResultType Type;
};

template<typename T>
struct ResultOfTask_<Future<T>>
{
    typedef T Type;
};

template<typename F>
struct ResultOfTask
{
    typedef typename ResultOfTask_<typename ResultOf<F>::Type>::Type Type;
};
*/

template<typename T>
struct StripFuture
{
    typedef T Type;
};

template<typename T>
struct StripFuture<Future<T>>
{
    typedef T Type;
};

template<typename F>
struct ReturnOfTask : ReturnOfTask<decltype(&F::operator())>
{};

template<typename ClassType, typename ReturnType>
struct ReturnOfTask<ReturnType (ClassType::*)() const>
{
    typedef ReturnType Type;
};

template<typename ClassType, typename ReturnType, typename A0>
struct ReturnOfTask<ReturnType (ClassType::*)(A0) const>
{
    typedef ReturnType Type;
};

template<typename F>
struct ResultOfTask
{
    typedef typename StripFuture<typename ReturnOfTask<F>::Type>::Type Type;
};


struct TaskResultClassVoid {};
struct TaskResultClassGeneric {};
struct TaskResultClassFuture {};

template<typename ResultType, typename ReturnType>
struct TaskResultClass
{
    typedef TaskResultClassGeneric Type;
};

template<>
struct TaskResultClass<void, void>
{
    typedef TaskResultClassVoid Type;
};

template<typename R>
struct TaskResultClass<R, Future<R>>
{
    typedef TaskResultClassFuture Type;
};

struct TaskCallClassVoid {};
struct TaskCallClassExecutionContext {};


template<typename F>
struct TaskCallClass : TaskCallClass<decltype(&F::operator())>
{};

template<typename ClassType, typename ReturnType>
struct TaskCallClass<ReturnType (ClassType::*)() const>
{
    typedef TaskCallClassVoid Type;
};

template<typename ClassType, typename ReturnType, typename A0>
struct TaskCallClass<ReturnType (ClassType::*)(A0) const>
{
    typedef TaskCallClassExecutionContext Type;
};


template<typename F>
struct TaskTraits
{
    typedef typename ReturnOfTask<F>::Type ReturnType;
    typedef typename ResultOfTask<F>::Type ResultType;
    typedef typename TaskResultClass<ResultType, ReturnType>::Type ResultClass;
    typedef typename TaskCallClass<F>::Type CallClass;
};

}}}

#endif
