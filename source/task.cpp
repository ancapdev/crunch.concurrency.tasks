// Copyright (c) 2011, Christian Rorvik
// Distributed under the Simplified BSD License (See accompanying file LICENSE.txt)

#include "crunch/concurrency/task.hpp"

// TODO: remove
#include "crunch/concurrency/future.hpp"

namespace Crunch { namespace Concurrency {

    

// MOCKUP

template<typename T>
struct FutureType
{
    typedef Future<T> Type;
};

template<typename T>
struct FutureType<Future<T>>
{
    typedef typename FutureType<T>::Type Type;
};

class Scheduler
{
public:

    /*
    Unwrap Future<T> for continuation passing style tasks.
    Example:
    Future<int> Do()
    {
        // Returned future is considered a continuation by the scheduler
        // Knows at the point of scheduling Do() that it will have continuation.
        // Might still want explicit ContinueWith
        // - Future<int> does not give the scheduler the underlying Task (and there's no guarantee it is a Task)
        // - Scheduler must know at the point of adding the task:
        //   - Not to run it until the current task is complete -- could leave it to the programmer to add appropriate barrier if necessary
        //   - To associate result storage with the current task
        //     - if copying is permissible from a performance point of view, could create the continuation to forward the result internally in the scheduler, with a dependency on the returned future
        //     - an explicit continuation could be used where performance is required, which result will be written directly into that of the parent task
        //     - Scheduler for task<Future<T>> would do:
        //       Future<T> f = t.run();
        //       if (!has continuation) // if we had the distinction between Future and SharedFuture then the result could be moved rather than copied.
        //           ContinueWith([] { f.Get(); }, &f, 1);
        //       else
        //           assert f.GetData() == t.GetData()
        return Scheduler::Add(&DoInternal);
    }
    int DoInternal();
   
    Future<int> t1 = Scheduler::Add(&Do);
    Future<int> t2 = Scheduler::Add(&DoInternal); 
    Future<int> t3 = Do();
    t1, t2, and t3 are equivalent
    */
    template<typename F>
    auto Add(F f, IWaitable** dependencies, std::size_t dependencyCount) -> typename FutureType<decltype(f())>::Type;

    template<typename F>
    auto Add(F f) -> typename FutureType<decltype(f())>::Type;

    // If we need to return the actual Future result
    template<typename F>
    auto AddNoUnwrap(F f, IWaitable** dependencies, std::size_t dependencyCount) -> Future<decltype(f())>;

    // Only valid within a task execution
    template<typename F>
    auto ContinueWith(F f, IWaitable** dependencies, std::size_t dependencyCount) -> typename FutureType<decltype(f())>::Type;
};

}}


