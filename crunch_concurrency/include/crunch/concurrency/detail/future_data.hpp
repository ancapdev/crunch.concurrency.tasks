#ifndef CRUNCH_CONCURRENCY_DETAIL_FUTURE_DATA_HPP
#define CRUNCH_CONCURRENCY_DETAIL_FUTURE_DATA_HPP

#include "crunch/base/override.hpp"
#include "crunch/base/platform.hpp"
#include "crunch/base/stdint.hpp"
#include "crunch/concurrency/atomic.hpp"
#include "crunch/concurrency/event.hpp"

#include <cstddef>
#include <exception>
#include <memory>
#include <type_traits>
#include <utility>

namespace Crunch { namespace Concurrency { namespace Detail {

// Compose via inheritance to save extra vtable pointer in Event
class FutureDataBase : protected Event
{
public:
    using Event::AddWaiter;
    using Event::RemoveWaiter;

    FutureDataBase() : mRefCount(0, MEMORY_ORDER_RELEASE) {}

    void SetException(std::exception_ptr const& exception)
    {
        CRUNCH_ASSERT(!Event::IsSet());
        mException.reset(new std::exception_ptr(exception));
        Event::Set();
    }

    bool IsReady() const
    {
        return Event::IsSet();
    }

    bool HasValue() const
    {
        return IsReady() && !mException;
    }

    bool HasException() const
    {
        return IsReady() && mException;
    }

    void Wait()
    {
        if (!IsSet())
            WaitFor(*this);
    }
    
protected:
    friend void AddRef(FutureDataBase*);
    friend void Release(FutureDataBase*);

    virtual void Destroy();

    Atomic<uint32> mRefCount;
    std::unique_ptr<std::exception_ptr> mException;
};

inline void AddRef(FutureDataBase* object)
{
    object->mRefCount.Increment();
}

inline void Release(FutureDataBase* object)
{
    if (1 == object->mRefCount.Decrement())
        object->Destroy();
}

template<typename T>
class FutureData : public FutureDataBase
{
public:
    virtual ~FutureData()
    {
        if (HasValue())
            GetValue().~T();
    }

    void Set(T const& value)
    {
        CRUNCH_ASSERT(!Event::IsSet());
        ::new (ResultAddress()) T(value);
        Event::Set();
    }

    void Set(T&& value)
    {
        CRUNCH_ASSERT(!Event::IsSet());
        ::new (ResultAddress()) T(std::move(value));
        Event::Set();
    }

    T& Get()
    {
        Wait();

        if (!mException)
            return GetValue();

#if defined (CRUNCH_PLATFORM_WIN32)
        RethrowException();
#else
        std::rethrow_exception(*mException);
#endif
    }

private:
#if defined (CRUNCH_PLATFORM_WIN32)
    __declspec(noreturn) void RethrowException() { std::rethrow_exception(*mException); }
#endif
    
    T& GetValue()
    {
        return *static_cast<T*>(ResultAddress());
    }

    typedef std::aligned_storage<sizeof(T), std::alignment_of<T>::value> StorageType;

    void* ResultAddress() { return static_cast<void*>(&mResult); }

    StorageType mResult;
};


}}}

#endif
