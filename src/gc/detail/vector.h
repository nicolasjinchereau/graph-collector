/*--------------------------------------------------------------*
*  Copyright (c) 2022 Nicolas Jinchereau. All rights reserved.  *
*--------------------------------------------------------------*/

#pragma once
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>

namespace gc::detail {

template<class T>
class vector
{
public:
    using value_type      = T;
    using pointer         = T*;
    using const_pointer   = const T*;
    using reference       = T&;
    using const_reference = const T&;
    using size_type       = size_t;
    using difference_type = ptrdiff_t;

private:
    template<class Vec>
    class vector_const_iterator
    {
        template<class U>
        friend class vector;
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type        = typename Vec::value_type;
        using difference_type   = typename Vec::difference_type;
        using pointer           = typename Vec::const_pointer;
        using reference         = typename Vec::const_reference;

        vector_const_iterator()
            : ptr() {}

        reference operator*() const {
            assert(ptr);
            return *ptr;
        }

        pointer operator->() const {
            return ptr;
        }

        vector_const_iterator& operator++() {
            ++ptr;
            return *this;
        }

        vector_const_iterator operator++(int) {
            vector_const_iterator tmp = *this;
            ++ptr;
            return tmp;
        }

        vector_const_iterator& operator--() {
            --ptr;
            return *this;
        }

        vector_const_iterator operator--(int) {
            vector_const_iterator tmp = *this;
            --ptr;
            return tmp;
        }

        vector_const_iterator& operator+=(difference_type off) {
            ptr += off;
            return *this;
        }

        vector_const_iterator operator+(difference_type off) const {
            vector_const_iterator tmp = *this;
            tmp.ptr += off;
            return tmp;
        }

        vector_const_iterator& operator-=(difference_type off) {
            ptr -= off;
            return *this;
        }

        vector_const_iterator operator-(difference_type off) const {
            const_iterator tmp = *this;
            tmp.ptr -= off;
            return tmp;
        }

        difference_type operator-(const vector_const_iterator& that) const {
            return ptr - that.ptr;
        }

        reference operator[](difference_type off) const {
            return ptr[off];
        }

        bool operator==(const vector_const_iterator& that) const {
            return ptr == that.ptr;
        }

        std::strong_ordering operator<=>(const vector_const_iterator& that) const {
            return ptr <=> that.ptr;
        }

    protected:
        T* ptr;

        vector_const_iterator(T* ptr)
            : ptr(ptr) {}
    };

    template<class Vec>
    class vector_iterator : public vector_const_iterator<Vec>
    {
        template<class U>
        friend class vector;
    public:
        using my_base = vector_const_iterator<Vec>;

        using iterator_category = std::random_access_iterator_tag;
        using value_type        = typename Vec::value_type;
        using difference_type   = typename Vec::difference_type;
        using pointer           = typename Vec::pointer;
        using reference         = typename Vec::reference;

        vector_iterator()
            : my_base() {}

        reference operator*() const {
            return const_cast<reference>(my_base::operator*());
        }

        pointer operator->() const {
            return this->ptr;
        }

        vector_iterator& operator++() {
            my_base::operator++();
            return *this;
        }

        vector_iterator operator++(int) {
            vector_iterator tmp = *this;
            my_base::operator++();
            return tmp;
        }

        vector_iterator& operator--() {
            my_base::operator--();
            return *this;
        }

        vector_iterator operator--(int) {
            vector_iterator tmp = *this;
            my_base::operator--();
            return tmp;
        }

        vector_iterator& operator+=(difference_type off) {
            my_base::operator+=(off);
            return *this;
        }

        vector_iterator operator+(difference_type off) const {
            return { this->ptr + off };
        }

        vector_iterator& operator-=(difference_type off) {
            my_base::operator-=(off);
            return *this;
        }

        vector_iterator operator-(difference_type off) const {
            return { this->ptr - off };
        }

        difference_type operator-(const vector_iterator& that) const {
            return this->ptr - that.ptr;
        }

        reference operator[](difference_type off) const {
            return const_cast<reference>(my_base::operator[](off));
        }

    private:
        vector_iterator(T* ptr)
            : my_base(ptr) {}
    };

public:
    using my_type = vector<T>;

    using iterator = vector_iterator<my_type>;
    using const_iterator = vector_const_iterator<my_type>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    vector()
    {
    }

    vector(size_t count)
    {
        if (count != 0) {
            create(count);
            mLast = std::uninitialized_value_construct_n(mFirst, count);
        }
    }

    vector(size_t count, const T& value)
    {
        if (count != 0) {
            create(count);
            mLast = std::uninitialized_fill_n(mFirst, count, value);
        }
    }

    template<std::input_iterator Iter>
    vector(Iter first, Iter last)
    {
        size_t count = std::distance(first, last);
        if (count != 0) {
            create(count);
            mLast = std::uninitialized_copy(first, last, mFirst);
        }
    }

    vector(std::initializer_list<T> init)
        : vector(init.begin(), init.end())
    {
    }

    vector(const vector& that)
        : vector(that.begin(), that.end())
    {
    }

    vector(vector&& that) {
        move_from(that);
    }

    ~vector() {
        cleanup();
    }

    vector& operator=(const vector& that)
    {
        if (this == &that)
            return *this;

        assign(that.begin(), that.end());
        return *this;
    }

    vector& operator=(vector&& that)
    {
        if (this == &that)
            return *this;

        cleanup();
        move_from(that);
        return *this;
    }

    vector& operator=(std::initializer_list<T> init) {
        assign(init.begin(), init.end());
        return *this;
    }

    template<std::input_iterator Iter>
    void assign(Iter first, Iter last)
    {
        if (!empty())
            std::destroy(mFirst, mLast);

        size_t cap = capacity();
        size_t count = std::distance(first, last);
        if (cap < count)
        {
            dealloc(mFirst);
            size_t cap = std::max(cap * 3 / 2, count);
            mFirst = alloc(cap);
            mEnd = mFirst + cap;
        }

        mLast = std::uninitialized_copy(first, last, mFirst);
    }

    void assign(size_t count, const T& value)
    {
        if (!empty())
            std::destroy(mFirst, mLast);

        size_t cap = capacity();
        if (cap < count)
        {
            dealloc(mFirst);
            size_t cap = std::max(cap * 3 / 2, count);
            mFirst = alloc(cap);
            mEnd = mFirst + cap;
        }

        mLast = std::uninitialized_fill_n(mFirst, count, value);
    }

    void assign(std::initializer_list<T> init) {
        assign(init.begin(), init.end());
    }

    template<typename... Args>
    T& emplace_back(Args&&... args) {
        reserve(size() + 1);
        return *std::construct_at(mLast++, std::forward<Args>(args)...);
    }

    void push_back(const T& value) {
        reserve(size() + 1);
        std::construct_at(mLast++, value);
    }

    void push_back(T&& value) {
        reserve(size() + 1);
        std::construct_at(mLast++, std::move(value));
    }

    void pop_back() {
        assert(!empty());
        std::destroy_at(mLast--);
    }

    void clear() {
        std::destroy(mFirst, mLast);
        mLast = mFirst;
    }

    void swap(vector& right) {
        std::swap(mFirst, right.mFirst);
        std::swap(mLast, right.mLast);
        std::swap(mEnd, right.MEnd);
    }

    void resize(size_t newSize) {
        resize_v(newSize);
    }

    void resize(size_t newSize, const T& value) {
        resize_v(newSize, value);
    }

    void reserve(size_t newCapacity)
    {
        size_t currentCap = capacity();
        if (newCapacity <= currentCap)
            return;

        newCapacity = std::max(currentCap * 3 / 2, newCapacity);
        assert(newCapacity <= max_size());
        assert(newCapacity >= size());

        reallocate(newCapacity);
    }

    void shrink_to_fit()
    {
        if (mLast != mEnd)
        {
            if (empty())
                cleanup();
            else
                reallocate(size());
        }
    }

    iterator begin() {
        return { mFirst };
    }

    const_iterator begin() const {
        return { mFirst };
    }

    iterator end() {
        return { mLast };
    }

    const_iterator end() const {
        return { mLast };
    }

    reverse_iterator rbegin() {
        return reverse_iterator(end());
    }

    const_reverse_iterator rbegin() const {
        return const_reverse_iterator(end());
    }

    reverse_iterator rend() {
        return reverse_iterator(begin());
    }

    const_reverse_iterator rend() const {
        return const_reverse_iterator(begin());
    }

    template<class U> requires std::constructible_from<T, U>
    iterator insert(const_iterator where, U&& value)
    {
        assert(where.ptr >= mFirst);
        assert(where.ptr <= mLast);

        ptrdiff_t whereOff = where.ptr - mFirst;
        reserve(size() + 1);

        T* wherePtr = mFirst + whereOff;

        if (wherePtr == mLast) {
            std::construct_at(mLast++, std::forward<U>(value));
        }
        else {
            std::construct_at(mLast, std::move(*(mLast - 1)));
            T* oldLast = mLast++;
            std::move_backward(wherePtr, oldLast - 1, oldLast);
            std::construct_at(wherePtr, std::forward<U>(value));
        }

        return { wherePtr };
    }

    template<std::input_iterator Iter>
    iterator insert(const_iterator where, Iter first, Iter last)
    {
        assert(where.ptr >= mFirst);
        assert(where.ptr <= mLast);

        ptrdiff_t whereOff = where.ptr - mFirst;

        size_t count = std::distance(first, last);
        reserve(size() + count);

        T* wherePtr = mFirst + whereOff;

        if (wherePtr == mLast) {
            mLast = std::uninitialized_copy(first, last, mLast);
        }
        else
        {
            T* newLast = mLast + count;
            std::uninitialized_value_construct(mLast, newLast);
            mLast = newLast;
            std::move_backward(wherePtr, mLast - count, mLast);
            std::copy(first, last, wherePtr);
        }

        return { wherePtr };
    }

    iterator insert(const_iterator where, std::initializer_list<T> init) {
        return insert(where, init.begin(), init.end());
    }

    iterator erase(const_iterator where)
    {
        assert(where.ptr >= mFirst);
        assert(where.ptr < mLast);

        std::move(where.ptr + 1, mLast, where.ptr);
        std::destroy_at(mLast - 1);
        --mLast;
        return { where.ptr };
    }

    iterator erase(const_iterator begin, const_iterator end)
    {
        assert(begin.ptr <= end.ptr);
        assert(begin.ptr >= mFirst);
        assert(end.ptr <= mLast);

        if (begin.ptr != end.ptr) {
            T* newLast = std::move(end.ptr, mLast, begin.ptr);
            std::destroy(newLast, mLast);
            mLast = newLast;
        }

        return { begin.ptr };
    }

    T* data() {
        return mFirst;
    }

    const T* data() const {
        return mFirst;
    }

    bool empty() const {
        return mFirst == mLast;
    }

    size_t size() const {
        return mLast - mFirst;
    }

    size_t capacity() const {
        return mEnd - mFirst;
    }

    size_t max_size() const {
        return std::numeric_limits<ptrdiff_t>::max();
    }

    T& at(size_t pos) {
        assert(pos < size());
        return mFirst[pos];
    }

    const T& at(size_t pos) const {
        return const_cast<vector<T>>(this)->at(pos);
    }

    T& operator[](size_t pos) {
        return at(pos);
    }

    const T& operator[](size_t pos) const {
        return const_cast<vector<T>>(this)->at(pos);
    }

    T& front() {
        assert(!empty());
        return *mFirst;
    }

    const T& front() const {
        return const_cast<vector<T>>(this)->front();
    }

    T& back() {
        assert(!empty());
        return *(mLast - 1);
    }

    const T& back() const {
        return const_cast<vector<T>>(this)->back();
    }

private:
    static T* alloc(size_t count) {
        void* p = ::operator new(count * sizeof(T), std::align_val_t(alignof(T)));
        return static_cast<T*>(p);
    }

    static void dealloc(T* p) noexcept {
        ::operator delete(static_cast<void*>(p), std::align_val_t(alignof(T)));
    }

    struct Deleter {
        void operator()(T* p) const noexcept {
            dealloc(p);
        }
    };

    void create(size_t cap)
    {
        assert(mFirst == nullptr);
        mFirst = alloc(cap);
        mLast = mFirst;
        mEnd = mFirst + cap;
    }

    void cleanup()
    {
        if (mFirst)
        {
            std::destroy(mFirst, mLast);
            dealloc(mFirst);

            mFirst = nullptr;
            mLast = nullptr;
            mEnd = nullptr;
        }
    }

    void reallocate(size_t exactCapacity)
    {
        size_t currentCap = capacity();
        if (currentCap == exactCapacity)
            return;

        std::unique_ptr<T, Deleter> newVec;

        if (exactCapacity != 0) {
            newVec.reset(alloc(exactCapacity));
        }

        size_t currentSize = size();
        if (currentSize)
        {
            if constexpr (std::is_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
                std::uninitialized_move(mFirst, mLast, newVec.get());
            }
            else {
                std::uninitialized_copy(mFirst, mLast, newVec.get());
            }

            std::destroy(mFirst, mLast);
            dealloc(mFirst);
        }

        mFirst = newVec.release();
        mLast = mFirst + currentSize;
        mEnd = mFirst + exactCapacity;
    }

    void move_from(vector& right) {
        mFirst = right.mFirst;
        mLast = right.mLast;
        mEnd = right.mEnd;
        right.mFirst = nullptr;
        right.mLast = nullptr;
        right.mEnd = nullptr;
    }

    template<class... U>
    void resize_v(size_t newSize, U&&... value)
    {
        size_t sz = size();
        if (newSize > sz)
        {
            reserve(newSize);
            T* newLast = mLast + (newSize - sz);

            if constexpr (sizeof...(U) == 0) {
                std::uninitialized_value_construct(mLast, newLast);
            }
            else {
                std::uninitialized_fill(mLast, newLast, std::forward<U>(value)...);
            }

            mLast = newLast;
        }
        else if (newSize < sz)
        {
            T* newLast = mLast - (sz - newSize);
            std::destroy(newLast, mLast);
            mLast = newLast;
        }
    }

    T* mFirst{};
    T* mLast{};
    T* mEnd{};
};

} // gc::detail
