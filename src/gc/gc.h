/*--------------------------------------------------------------*
*  Copyright (c) 2022 Nicolas Jinchereau. All rights reserved.  *
*--------------------------------------------------------------*/

#pragma once
#include <algorithm>
#include <cassert>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <shared_mutex>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <gc/detail/functor.h>
#include <gc/detail/intrusive_list.h>
#include <gc/detail/vector.h>

namespace gc {

template<class T>
class graph_ptr;

template<class T>
class weak_graph_ptr;

template<class T>
class raw_graph_ptr;

struct memory_range
{
    std::byte* begin{};
    std::byte* end{};
};

struct scan_info
{
    void* gp;
    memory_range range;
    bool managed;
};

class garbage
{
    detail::vector<std::shared_ptr<void>> unreachable_objects;

    garbage(detail::vector<std::shared_ptr<void>>&& objs)
        : unreachable_objects(std::move(objs)){}

    friend class graph;

public:
    garbage() = default;

    garbage(const garbage&) = delete;
    garbage& operator=(const garbage&) = delete;

    garbage(garbage&&) = default;
    garbage& operator=(garbage&&) = default;
};

class graph
{
    template<class U>
    friend class graph_ptr;

    template<class U>
    friend class weak_graph_ptr;

    template<class U>
    friend class raw_graph_ptr;

    template <class U>
    friend class allocator;

    std::recursive_mutex graphLock;
    std::shared_mutex pointerLock;
    detail::intrusive_list<graph_ptr<void>> pointers;
    detail::intrusive_list<raw_graph_ptr<void>> rawPointers;
    detail::vector<memory_range> ranges;

    std::chrono::steady_clock::time_point collectionStart;
    detail::vector<scan_info> info;
    detail::vector<uint32_t> scan;
    detail::vector<uint32_t> keep;
    std::atomic<bool> collecting = false;

    graph();
    ~graph();

    graph(const graph&) = delete;
    graph(graph&&) = delete;
    graph& operator=(const graph&) = delete;
    graph& operator=(graph&&) = delete;
    
    static struct That { graph* operator->(); } that;
public:

    // create and construct a ref-counted traceable object
    template<class T, typename... Args> requires (!std::is_array_v<T>)
    static graph_ptr<T> make(Args&&... args);

    // allocate traceable raw memory
    template<class T>
    static raw_graph_ptr<T> allocate(size_t count);

    // deallocate memory previously allocated by a call to 'allocate'
    template<class T>
    static void deallocate(const raw_graph_ptr<T>& up);

    // collect unreachable graph_ptr<T> objects and return them
    // to the caller so they can destroy them where appropriate
    [[nodiscard]] static garbage collect();

    // get total number of objects owned by this graph
    static int allocated_objects();
    
    // get total bytes allocated by this graph
    static size_t allocated_bytes();

private:
    void attach(graph_ptr<void>* gp);
    void detach(graph_ptr<void>* gp);
    
    void attach(raw_graph_ptr<void>* gp);
    void detach(raw_graph_ptr<void>* gp);

    void add_range(void* p, size_t size);
    void remove_range(void* p);

    std::optional<memory_range> find_range(void* internalPtr);
    detail::vector<memory_range>::iterator find_range_iterator(void* internalPtr);

    garbage collect_impl();
};

template<class T>
concept graph_object_subclass = std::is_base_of_v<typename T::graph_object_type, T>;

template<class T, typename... Args> requires (!std::is_array_v<T>)
inline graph_ptr<T> graph::make(Args&&... args)
{
    struct graph_ptr_storage
    {
        union { T storage; };

        graph_ptr_storage(Args&&... args) {
            graph::that->add_range(static_cast<void*>(&storage), sizeof(T));
            std::construct_at(&storage, std::forward<Args>(args)...);
        }
        ~graph_ptr_storage() {
            std::destroy_at(&storage);
            graph::that->remove_range(static_cast<void*>(&storage));
        }
    };

    std::shared_ptr<graph_ptr_storage> gps = std::make_shared<graph_ptr_storage>(std::forward<Args>(args)...);
    T* p = &gps->storage;
    std::shared_ptr<T> sp = { std::move(gps), p };

    graph_ptr<T> ret = { std::move(sp) };

    if constexpr (graph_object_subclass<T>) {
        ret->wp = ret.ptr;
    }
    
    return ret;
}

template<class T>
inline raw_graph_ptr<T> graph::allocate(size_t count)
{
    size_t size = sizeof(T) * count;
    size_t align = alignof(T);

    void* p = ::operator new(size, std::align_val_t(align));
    that->add_range(p, size);

    return { static_cast<T*>(p) };
}

template<class T>
inline void graph::deallocate(const raw_graph_ptr<T>& gp)
{
    if(gp.ptr) {
        that->remove_range(gp.ptr);
        ::operator delete(gp.ptr, std::align_val_t(alignof(T)));
    }
}

template<class T>
class graph_ptr final : public detail::intrusive_list_node
{
public:
    friend graph;

    template <class U>
    friend class graph_ptr;

    template<class U>
    friend class weak_graph_ptr;

    template <class U>
    friend class allocator;

    template<class U>
    friend class graph_object;
private:
    std::shared_ptr<T> ptr;

    void attach() { graph::that->attach((graph_ptr<void>*)this); }
    void detach() { graph::that->detach((graph_ptr<void>*)this); }

    graph_ptr(std::shared_ptr<T>&& ptr)
        : ptr(std::move(ptr)) { attach(); }

    template<class U>
    graph_ptr(std::shared_ptr<U>&& sp, T* p)
        : ptr(std::move(sp), p) { attach(); }
public:
    using element_type = std::remove_extent_t<T>;

    graph_ptr() : ptr() { attach(); }
    graph_ptr(std::nullptr_t) : ptr() { attach(); }

    template<class U>
    graph_ptr(const graph_ptr<U>& that, T* p)
        : ptr(that.ptr, p) { attach(); }

    template<class U>
    graph_ptr(graph_ptr<U>&& that, T* p) {
        attach();
        std::shared_lock lk(graph::that->pointerLock);
        ptr = { std::move(that.ptr), p };
    }

    graph_ptr(const graph_ptr& that)
        : ptr(that.ptr) { attach(); }

    template<class U> requires std::is_convertible_v<U*, element_type*>
    graph_ptr(const graph_ptr<U>& that)
        : ptr(that.ptr) { attach(); }

    graph_ptr(graph_ptr&& that) {
        attach();
        std::shared_lock lk(graph::that->pointerLock);
        ptr = std::move(that.ptr);
    }

    template<class U> requires std::is_convertible_v<U*, element_type*>
    graph_ptr(graph_ptr<U>&& that) {
        attach();
        std::shared_lock lk(graph::that->pointerLock);
        ptr = std::move(that.ptr);
    }

    template<class U> requires std::is_convertible_v<U*, element_type*>
    explicit graph_ptr(const weak_graph_ptr<U>& that)
        : ptr(that.ptr) { attach(); }

    ~graph_ptr() { detach(); }

    graph_ptr& operator=(const graph_ptr& that) {
        std::shared_lock lk(graph::that->pointerLock);
        ptr = that.ptr;
        return *this;
    }

    template<class U>
    graph_ptr& operator=(const graph_ptr<U>& that) {
        std::shared_lock lk(graph::that->pointerLock);
        ptr = that.ptr;
        return *this;
    }

    graph_ptr& operator=(graph_ptr&& that) {
        std::shared_lock lk(graph::that->pointerLock);
        ptr = std::move(that.ptr);
        return *this;
    }

    template<class U>
    graph_ptr& operator=(graph_ptr<U>&& that) {
        std::shared_lock lk(graph::that->pointerLock);
        ptr = std::move(that.ptr);
        return *this;
    }

    graph_ptr& operator=(std::nullptr_t) {
        reset();
        return *this;
    }

    void swap(graph_ptr& that) {
        std::shared_lock lk(graph::that->pointerLock);
        ptr.swap(that.ptr);
    }

    void reset() {
        std::shared_lock lk(graph::that->pointerLock);
        ptr.reset();
    }

    T* get() const {
        return ptr.get();
    }

    template<class R = T> requires (!(std::is_array_v<R> || std::is_void_v<R>))
    R& operator*() const {
        return *ptr;
    }

    template<class R = T> requires (!std::is_array_v<R>)
    R* operator->() const {
        return ptr.operator->();
    }

    template<class U = T, class R = element_type> requires std::is_array_v<U>
    R& operator[](ptrdiff_t index) const {
        return get()[index];
    }

    explicit operator bool() const {
        return ptr.operator bool();
    }

    int use_count() const {
        return ptr.use_count();
    }
};

template<class T1, class T2>
inline bool operator==(const graph_ptr<T1>& left, const graph_ptr<T2>& right) {
    return left.get() == right.get();
}

template<class T1>
inline bool operator==(const graph_ptr<T1>& left, std::nullptr_t) {
    return left.get() == nullptr;
}

template<class T1, class T2>
inline std::strong_ordering operator<=>(
    const graph_ptr<T1>& left, const graph_ptr<T2>& right) {
    return left.get() <=> right.get();
}

template<class T1>
inline std::strong_ordering operator<=>(
    const graph_ptr<T1>& left, std::nullptr_t) {
    return left.get() <=> nullptr;
}

template<class T1, class T2>
graph_ptr<T1> static_pointer_cast(const graph_ptr<T2>& other) noexcept {
    const auto p = static_cast<typename graph_ptr<T1>::element_type*>(other.get());
    return graph_ptr<T1>(other, p);
}

template<class T1, class T2>
graph_ptr<T1> static_pointer_cast(graph_ptr<T2>&& other) noexcept {
    const auto p = static_cast<typename graph_ptr<T1>::element_type*>(other.get());
    return graph_ptr<T1>(std::move(other), p);
}

template<class T1, class T2>
graph_ptr<T1> const_pointer_cast(const graph_ptr<T2>& other) noexcept {
    const auto p = const_cast<typename graph_ptr<T1>::element_type*>(other.get());
    return graph_ptr<T1>(other, p);
}

template<class T1, class T2>
graph_ptr<T1> const_pointer_cast(graph_ptr<T2>&& other) noexcept {
    const auto p = const_cast<typename graph_ptr<T1>::element_type*>(other.get());
    return graph_ptr<T1>(std::move(other), p);
}

template<class T1, class T2>
graph_ptr<T1> reinterpret_pointer_cast(const graph_ptr<T2>& other) noexcept {
    const auto p = reinterpret_cast<typename graph_ptr<T1>::element_type*>(other.get());
    return graph_ptr<T1>(other, p);
}

template<class T1, class T2>
graph_ptr<T1> reinterpret_pointer_cast(graph_ptr<T2>&& other) noexcept {
    const auto p = reinterpret_cast<typename graph_ptr<T1>::element_type*>(other.get());
    return graph_ptr<T1>(std::move(other), p);
}

template<class T1, class T2>
graph_ptr<T1> dynamic_pointer_cast(const graph_ptr<T2>& other) noexcept {
    const auto p = dynamic_cast<typename graph_ptr<T1>::element_type*>(other.get());
    if (p) return graph_ptr<T1>(other, p);
    return {};
}

template<class T1, class T2>
graph_ptr<T1> dynamic_pointer_cast(graph_ptr<T2>&& other) noexcept {
    const auto p = dynamic_cast<typename graph_ptr<T1>::element_type*>(other.get());
    if (p) return graph_ptr<T1>(std::move(other), p);
    return {};
}

template<class T>
class weak_graph_ptr final
{
public:
    using P = std::remove_extent_t<T>;

    friend graph;

private:
    template<class U>
    friend class graph_ptr;

    template<class U>
    friend class weak_graph_ptr;

    template<class U>
    friend class allocator;

    template<class U>
    friend class graph_object;

    std::weak_ptr<T> ptr;

    weak_graph_ptr(std::weak_ptr<T>&& ptr)
        : ptr(std::move(ptr)){}
public:

    weak_graph_ptr() : ptr() {}
    weak_graph_ptr(std::nullptr_t) : ptr() {}
    weak_graph_ptr(const weak_graph_ptr& that) : ptr(that.ptr) {}

    template<class U> requires std::is_convertible_v<U*, T*>
    weak_graph_ptr(const weak_graph_ptr<U>& that) : ptr(that.ptr) {}

    weak_graph_ptr(weak_graph_ptr&& that) : ptr(std::move(that.ptr)) {}

    template<class U> requires std::is_convertible_v<U*, T*>
    weak_graph_ptr(weak_graph_ptr<U>&& that) : ptr(std::move(that.ptr)) {}

    template<class U> requires std::is_convertible_v<U*, T*>
    weak_graph_ptr(const graph_ptr<U>& that) : ptr(that.ptr) {}

    ~weak_graph_ptr() {}

    weak_graph_ptr& operator=(const weak_graph_ptr& that) {
        ptr = that.ptr;
        return *this;
    }

    template<class U> requires std::is_convertible_v<U*, T*>
    weak_graph_ptr& operator=(const weak_graph_ptr<U>& that) {
        ptr = that.ptr;
        return *this;
    }

    weak_graph_ptr& operator=(weak_graph_ptr&& that) {
        ptr = std::move(that.ptr);
        return *this;
    }

    template<class U>
    weak_graph_ptr& operator=(weak_graph_ptr<U>&& that) {
        ptr = std::move(that.ptr);
        return *this;
    }

    template<class U> requires std::is_convertible_v<U*, T*>
    weak_graph_ptr& operator=(const graph_ptr<U>& that) {
        ptr = that.ptr;
        return *this;
    }

    weak_graph_ptr& operator=(std::nullptr_t) {
        reset();
        return *this;
    }

    void swap(weak_graph_ptr& that) {
        std::swap(ptr, that.ptr);
    }

    void reset() {
        ptr.reset();
    }

    int use_count() const {
        return ptr.use_count();
    }

    bool expired() const {
        return ptr.expired();
    }

    graph_ptr<T> lock() const {
        return { ptr.lock() };
    }
};

template<class T>
class graph_object
{
    friend class graph;

    template<class T>
    friend class allocator;

    template<class U>
    friend class graph_ptr;

    mutable std::weak_ptr<T> wp{};

protected:
    graph_object(){}
    graph_object(const graph_object&){}
    ~graph_object() {}
    graph_object& operator=(const graph_object&) { return *this; }

public:
    using graph_object_type = graph_object<T>;

    template<class This>
    graph_ptr<This> self(This* ptr) {
        return graph_ptr<This>(std::shared_ptr<T>(wp), ptr);
    }

    template<class This>
    graph_ptr<const This> self(const This* ptr) const {
        return graph_ptr<const This>(std::shared_ptr<const T>(wp), ptr);
    }
};

template<class T>
class raw_graph_ptr final : public detail::intrusive_list_node
{
public:
    using P = std::remove_extent_t<T>;

    friend graph;

    template <class U>
    friend class allocator;

    template<class U>
    friend class graph_object;

    template<class U>
    friend class raw_graph_ptr;

    using value_type = T;
    using pointer = raw_graph_ptr<T>;
    using difference_type = ptrdiff_t;
    using iterator_category = std::random_access_iterator_tag;

    template<class U = T> requires (!std::is_void_v<U>)
    using reference = U&;
private:
    P* ptr = nullptr;

    void attach() { graph::that->attach((raw_graph_ptr<void>*)this); }
    void detach() { graph::that->detach((raw_graph_ptr<void>*)this); }

    raw_graph_ptr(P* p) : ptr(p) { attach(); }
public:

    raw_graph_ptr() { attach(); }
    raw_graph_ptr(std::nullptr_t) { attach(); }

    raw_graph_ptr(const raw_graph_ptr& that)
        : ptr(that.ptr) { attach(); }

    template<class U> requires std::is_convertible_v<U*, P*>
    raw_graph_ptr(const raw_graph_ptr<U>& that)
        : ptr(that.ptr) { attach(); }

    raw_graph_ptr(raw_graph_ptr&& that)
    {
        attach();
        std::shared_lock lk(graph::that->pointerLock);
        ptr = that.ptr;
        that.ptr = nullptr;
    }

    template<class U> requires std::is_convertible_v<U*, P*>
    raw_graph_ptr(raw_graph_ptr<U>&& that)
    {
        attach();
        std::shared_lock lk(graph::that->pointerLock);
        ptr = that.ptr;
        that.ptr = nullptr;
    }

    ~raw_graph_ptr() { detach(); }

    raw_graph_ptr& operator=(const raw_graph_ptr& that) {
        std::shared_lock lk(graph::that->pointerLock);
        ptr = that.ptr;
        return *this;
    }

    template<class U>
    raw_graph_ptr& operator=(const raw_graph_ptr<U>& that) {
        std::shared_lock lk(graph::that->pointerLock);
        ptr = that.ptr;
        return *this;
    }

    raw_graph_ptr& operator=(raw_graph_ptr&& that) {
        std::shared_lock lk(graph::that->pointerLock);
        ptr = that.ptr;
        that.ptr = nullptr;
        return *this;
    }

    template<class U>
    raw_graph_ptr& operator=(raw_graph_ptr<U>&& that) {
        std::shared_lock lk(graph::that->pointerLock);
        ptr = that.ptr;
        that.ptr = nullptr;
        return *this;
    }

    raw_graph_ptr& operator=(std::nullptr_t) {
        reset();
        return *this;
    }

    void swap(raw_graph_ptr& that) {
        std::shared_lock lk(graph::that->pointerLock);
        std::swap(ptr, that.ptr);
    }

    void reset() {
        std::shared_lock lk(graph::that->pointerLock);
        ptr = nullptr;
    }

    P* get() const {
        return ptr;
    }

    template<class R = T> requires (!std::is_void_v<R>)
    R& operator*() const {
        return *get();
    }

    P* operator->() const {
        return get();
    }

    template<class R = T> requires (!std::is_void_v<R>)
    R& operator[](ptrdiff_t index) const {
        return get()[index];
    }

    explicit operator bool() const {
        return get() != nullptr;
    }

    raw_graph_ptr& operator+=(ptrdiff_t offset) {
        std::shared_lock lk(graph::that->pointerLock);
        ptr += offset;
        return *this;
    }

    raw_graph_ptr& operator-=(ptrdiff_t offset) {
        std::shared_lock lk(graph::that->pointerLock);
        ptr -= offset;
        return *this;
    }

    raw_graph_ptr& operator++() {
        std::shared_lock lk(graph::that->pointerLock);
        ++ptr;
        return *this;
    }

    raw_graph_ptr operator++(int) {
        auto prev = ptr;
        operator++();
        return { prev };
    }

    raw_graph_ptr& operator--() {
        std::shared_lock lk(graph::that->pointerLock);
        --ptr;
        return *this;
    }

    raw_graph_ptr operator--(int) {
        auto prev = ptr;
        operator--();
        return { prev };
    }

    raw_graph_ptr operator+(ptrdiff_t offset) const {
        return { ptr + offset };
    }

    raw_graph_ptr operator-(ptrdiff_t offset) const {
        return { ptr - offset };
    }

    ptrdiff_t operator-(const raw_graph_ptr& that) const {
        return (ptr - that.ptr);
    }

    template<class U> requires (!std::is_void_v<U>&& std::is_same_v<U, T>)
    static raw_graph_ptr<U> pointer_to(U& u) {
        return raw_graph_ptr<U>(&u);
    }
};

template<class T1, class T2>
inline bool operator==(
    const raw_graph_ptr<T1>& left, const raw_graph_ptr<T2>& right)
{
    return left.get() == right.get();
}

template<class T1>
inline bool operator==(const raw_graph_ptr<T1>& left, std::nullptr_t) {
    return left.get() == nullptr;
}

template<class T1, class T2>
inline std::strong_ordering operator<=>(
    const raw_graph_ptr<T1>& left, const raw_graph_ptr<T2>& right)
{
    return left.get() <=> right.get();
}

template<class T1>
inline std::strong_ordering operator<=>(
    const raw_graph_ptr<T1>& left, std::nullptr_t)
{
    return left.get() <=> nullptr;
}

template <class T>
class allocator
{
public:
    using value_type = T;
    using pointer = raw_graph_ptr<T>;
    using const_pointer = raw_graph_ptr<const T>;
    using difference_type = ptrdiff_t;
    using size_type = std::size_t;

    template <class U>
    struct rebind {
        using other = allocator<U>;
    };

    allocator() {}

    template<class U>
    allocator(const allocator<U>& that) {}

    pointer allocate(size_type n) {
        return graph::allocate<T>(n);
    }

    void deallocate(pointer gp, size_type n) {
        graph::deallocate(gp);
    }

    allocator select_on_container_copy_construction() const {
        return *this;
    }

    using propagate_on_container_copy_assignment = std::false_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap = std::true_type;
    using is_always_equal = std::true_type;
};

template <class T1, class T2>
inline bool operator==(const allocator<T1>& a, const allocator<T2>& b) noexcept {
    return true;
}

template <class T1, class T2>
inline bool operator!=(const allocator<T1>& a, const allocator<T2>& b) noexcept {
    return false;
}

template<class T>
using vector = std::vector<T, allocator<T>>;

template<class T>
using list = std::list<T, allocator<T>>;

template<class T, class Comp = std::less<T>>
using priority_queue = std::priority_queue<T, std::vector<T, allocator<T>>, Comp>;

template<class K, class H = std::hash<K>, class E = std::equal_to<K>>
using unordered_set = std::unordered_set<K, H, E, allocator<K>>;

template<class K, class T, class H = std::hash<K>, class E = std::equal_to<K>>
using unordered_map = std::unordered_map<K, T, H, E, allocator<std::pair<const K, T>>>;

template<class F>
using function = detail::functor<allocator, F>;

} // gc
