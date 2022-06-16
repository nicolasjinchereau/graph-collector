/*--------------------------------------------------------------*
*  Copyright (c) 2022 Nicolas Jinchereau. All rights reserved.  *
*--------------------------------------------------------------*/

#pragma once
#include <gc/detail/compressed_pair.h>
#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace gc::detail {

template<class Functor, class F, class Ret, class... Args>
concept FunctorValue =
    !std::is_same_v<std::remove_cvref_t<F>, Functor> &&
    std::is_invocable_r_v<Ret, F, Args...>;

template<template<class> class Alloc, class Ret, class... Args>
class functor;

template<template<class> class Alloc, class Ret, class... Args>
class functor<Alloc, Ret(Args...)>
{
    struct Delegate
    {
        virtual ~Delegate() = default;
        virtual void DeleteThis(functor& fun) = 0;
        virtual void CopyConstructValue(functor& fun) = 0;
        virtual void MoveConstructValue(functor& fun) = 0;
        virtual void DestructiveMoveConstructPointer(functor& from, functor& to) = 0;
        virtual Ret Invoke(Args&&... args) = 0;
    };

    template<class T>
    struct DelegateInPlace : Delegate
    {
        T func;

        template<class U>
        DelegateInPlace(U&& fun)
            : func(std::forward<U>(fun)) {}

        virtual void DeleteThis(functor& fun) override {
            assert(0);
        }

        virtual void CopyConstructValue(functor& fun) override {
            fun.ValueConstruct(func);
        }

        virtual void MoveConstructValue(functor& fun) override {
            fun.ValueConstruct(std::move(func));
        }

        virtual void DestructiveMoveConstructPointer(functor& from, functor& to) override {
            assert(0);
        }

        virtual Ret Invoke(Args&&... args) override {
            return func(std::forward<Args>(args)...);
        }
    };

    template<class T>
    struct DelegateAlloc : Delegate
    {
        using AllocType = Alloc<DelegateAlloc<T>>;
        using AllocTraits = std::allocator_traits<AllocType>;
        using AllocPtrType = typename AllocTraits::pointer;

        compressed_pair<AllocType, T> data;

        template<class U>
        DelegateAlloc(const AllocType& al, U&& fun)
            : data(al, std::forward<U>(fun)) {}

        virtual void DeleteThis(functor& fun) override
        {
            assert(fun.GetDelegate() == static_cast<Delegate*>(this));
            AllocType al = data.first();
            std::destroy_at(this);

            AllocPtrType& fp = *static_cast<AllocPtrType*>(fun.GetLocalStorage());
            al.deallocate(fp, 1);
            std::destroy_at(&fp);
        }

        virtual void CopyConstructValue(functor& fun) override {
            fun.ValueConstruct(data.val2);
        }

        virtual void MoveConstructValue(functor& fun) override {
            fun.ValueConstruct(std::move(data.val2));
        }

        virtual void DestructiveMoveConstructPointer(functor& from, functor& to) override
        {
            AllocPtrType* fpFrom = static_cast<AllocPtrType*>(from.GetLocalStorage());
            AllocPtrType* fpTo = static_cast<AllocPtrType*>(to.GetLocalStorage());
            std::construct_at(fpTo, std::move(*fpFrom));
            std::destroy_at(fpFrom);
        }

        virtual Ret Invoke(Args&&... args) override {
            return data.val2(std::forward<Args>(args)...);
        }
    };

    constexpr static size_t NumPtrs = 10;
    constexpr static size_t StorageSize = (NumPtrs - 1) * sizeof(void*);

    union Storage {
        max_align_t dummy1;
        char dummy2[StorageSize];
        void* state[NumPtrs];
    } storage;

    void SetDelegate(Delegate* del) {
        storage.state[NumPtrs - 1] = del;
    }

    Delegate* GetDelegate() const {
        return static_cast<Delegate*>(storage.state[NumPtrs - 1]);
    }

    void* GetLocalStorage() {
        return static_cast<void*>(&storage.state[0]);
    }

    const void* GetLocalStorage() const {
        return static_cast<const void*>(&storage.state[0]);
    }

    bool IsStoredLocally() const {
        return static_cast<void*>(GetDelegate()) == GetLocalStorage();
    }

    void DefaultConstruct() {
        SetDelegate(nullptr);
    }

    template<class U>
    static auto Unfancy(U p) {
        return std::addressof(*p);
    }

    template<class U>
    static U* Unfancy(U* p) {
        return p;
    }

    template<class F> requires FunctorValue<functor, F, Ret, Args...>
    void ValueConstruct(F&& func)
    {
        using U = std::decay_t<F>;

        if constexpr (sizeof(DelegateInPlace<U>) <= StorageSize)
        {
            // construct payload in place
            DelegateInPlace<U>* p = new (GetLocalStorage()) DelegateInPlace<U>(std::forward<F>(func));
            SetDelegate(p);
        }
        else
        {
            // allocate payload on heap
            using AllocType = Alloc<DelegateAlloc<U>>;
            using AllocTraits = std::allocator_traits<AllocType>;
            using AllocPtrType = typename AllocTraits::pointer;

            AllocType al;
            AllocPtrType& fp = *new (GetLocalStorage()) AllocPtrType(al.allocate(1));
            DelegateAlloc<U>* p = Unfancy(fp);
            std::construct_at(p, al, std::forward<F>(func));
            SetDelegate(p);
        }
    }

    void Destruct()
    {
        Delegate* p = GetDelegate();

        if (p == GetLocalStorage()) {
            // local storage contains the delegate, destroy it
            std::destroy_at(p);
        }
        else if (p) {
            // local storage may contain a fancy pointer to the delegate,
            // so pass it to the delegate which knows what type to destroy
            p->DeleteThis(*this);
        }
    }

    void CopyConstruct(const functor& other)
    {
        if (other)
            other.GetDelegate()->CopyConstructValue(*this);
        else
            DefaultConstruct();
    }

    void MoveConstruct(functor&& other)
    {
        if (other)
        {
            if (other.IsStoredLocally())
            {
                other.GetDelegate()->MoveConstructValue(*this);
                other.Destruct();
                other.DefaultConstruct();
            }
            else
            {
                Delegate* del = other.GetDelegate();
                SetDelegate(del);
                del->DestructiveMoveConstructPointer(other, *this);
                other.DefaultConstruct();
            }
        }
        else
        {
            DefaultConstruct();
        }
    }

public:
    functor() {
        DefaultConstruct();
    }

    functor(std::nullptr_t) {
        DefaultConstruct();
    }

    template<class F> requires FunctorValue<functor, F, Ret, Args...>
    functor(F&& func) {
        ValueConstruct(std::forward<F>(func));
    }

    functor(const functor& other) {
        CopyConstruct(other);
    }

    functor(functor&& other) {
        MoveConstruct(std::move(other));
    }

    ~functor() {
        Destruct();
    }

    functor& operator=(std::nullptr_t) {
        Destruct();
        DefaultConstruct();
        return *this;
    }

    template<class F> requires FunctorValue<functor, F, Ret, Args...>
    functor& operator=(F&& func) {
        Destruct();
        ValueConstruct(std::forward<F>(func));
        return *this;
    }

    functor& operator=(const functor& other) {
        Destruct();
        CopyConstruct(other);
        return *this;
    }

    functor& operator=(functor&& other) {
        Destruct();
        MoveConstruct(std::move(other));
        return *this;
    }

    operator bool() const {
        return GetDelegate() != nullptr;
    }

    bool heap_allocated() const {
        return GetDelegate() != nullptr && GetDelegate() != GetLocalStorage();
    }

    Ret operator()(Args&&... args) const {
        Delegate* del = GetDelegate();
        if(!del) throw std::bad_function_call();
        return del->Invoke(std::forward<Args>(args)...);
    }

    bool operator==(std::nullptr_t) const {
        return GetDelegate() == nullptr;
    }

    bool operator!=(std::nullptr_t) const {
        return GetDelegate() != nullptr;
    }
};

} // gc::detail
