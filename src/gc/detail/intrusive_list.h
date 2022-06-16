/*--------------------------------------------------------------*
*  Copyright (c) 2022 Nicolas Jinchereau. All rights reserved.  *
*--------------------------------------------------------------*/

#pragma once
#include <cassert>
#include <iterator>
#include <type_traits>

namespace gc::detail {

class intrusive_list_node
{
private:
    template<class T>
    friend class intrusive_list;

    intrusive_list_node* next{};
    intrusive_list_node* prev{};

public:
    ~intrusive_list_node() {
        //assert(!next);
        //assert(!prev);
    }

    intrusive_list_node& operator=(const intrusive_list_node&) {
        return *this;
    }

    intrusive_list_node& operator=(intrusive_list_node&&) noexcept {
        return *this;
    }
};

template<class T>
class intrusive_list
{
    intrusive_list_node head;
    size_t count = 0;
public:

    using value_type = T;
    using reference = value_type&;
    using const_reference = const value_type&;

    class const_iterator
    {
    protected:
        intrusive_list_node* node;
        
        const_iterator(intrusive_list_node* node)
            : node(node)
        {
            assert(node);
        }

    public:
        friend class intrusive_list;

        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = T;
        using difference_type = ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;

        reference operator*() const {
            return *static_cast<const T*>(node);
        }

        pointer operator->() const {
            return static_cast<const T*>(node);
        }

        pointer get() const {
            return static_cast<const T*>(node);
        }

        const_iterator& operator++() {
            node = node->next;
            return *this;
        }

        const_iterator operator++(int) {
            auto tmp = *this;
            node = node->next;
            return tmp;
        }

        const_iterator& operator--() {
            node = node->prev;
            return *this;
        }

        const_iterator operator--(int) {
            auto tmp = *this;
            node = node->prev;
            return tmp;
        }

        bool operator==(const const_iterator& that) const {
            return node == that.node;
        }
    };

    class iterator : public const_iterator
    {
        iterator(intrusive_list_node* node)
            : const_iterator(node){}

    public:
        friend class intrusive_list;

        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = T;
        using difference_type = ptrdiff_t;
        using pointer = T*;
        using reference = T&;

        reference operator*() const {
            return const_cast<reference>(const_iterator::operator*());
        }

        pointer operator->() const {
            return const_cast<pointer>(const_iterator::operator->());
        }

        pointer get() const {
            return const_cast<pointer>(const_iterator::get());
        }

        iterator& operator++() {
            const_iterator::operator++();
            return *this;
        }

        iterator operator++(int) {
            auto tmp = *this;
            const_iterator::operator++();
            return tmp;
        }

        iterator& operator--() {
            const_iterator::operator--();
            return *this;
        }

        iterator operator--(int) {
            auto tmp = *this;
            const_iterator::operator--();
            return tmp;
        }
    };

    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    intrusive_list() {
        head.next = &head;
        head.prev = &head;
    }

    intrusive_list(const intrusive_list&) = delete;
    intrusive_list& operator=(const intrusive_list&) = delete;

    intrusive_list(intrusive_list&& that)
    {
        if (!that.empty())
        {
            head.prev = that.head.prev;
            head.next = that.head.next;
            head.prev->next = &head;
            head.next->prev = &head;
            that.head.next = &that.head;
            that.head.prev = &that.head;
        }
        else
        {
            head.prev = &head;
            head.next = &head;
        }
    }

    intrusive_list& operator=(intrusive_list&& that)
    {
        clear();

        if (!that.empty())
        {
            head.prev = that.head.prev;
            head.next = that.head.next;
            head.prev->next = &head;
            head.next->prev = &head;
            that.head.next = &that.head;
            that.head.prev = &that.head;
        }
        else
        {
            head.prev = &head;
            head.next = &head;
        }

        return *this;
    }

    ~intrusive_list() {
        // orphan all nodes
    }

    void push_front(T* node)
    {
        assert(node);
        //assert(!contains(node));
        assert(node->prev == nullptr);
        assert(node->next == nullptr);
        auto before = &head;
        auto after = head.next;
        before->next = node;
        node->prev = before;
        node->next = after;
        after->prev = node;
        ++count;
    }

    void push_back(T* node)
    {
        assert(node);
        //assert(!contains(node));
        assert(node->prev == nullptr);
        assert(node->next == nullptr);
        auto before = head.prev;
        auto after = &head;
        before->next = node;
        node->prev = before;
        node->next = after;
        after->prev = node;
        ++count;
    }

    void pop_front()
    {
        assert(!empty());
        auto node = head.next;
        auto before = node->prev;
        auto after = node->next;
        before->next = after;
        after->prev = before;
        node->prev = nullptr;
        node->next = nullptr;
        --count;
    }

    void pop_back()
    {
        assert(!empty());
        auto node = head.prev;
        auto before = node->prev;
        auto after = node->next;
        before->next = after;
        after->prev = before;
        node->prev = nullptr;
        node->next = nullptr;
        --count;
    }

    const_reference back() const {
        assert(!empty());
        return *static_cast<const T*>(head.prev);
    }

    reference back() {
        assert(!empty());
        return *static_cast<T*>(head.prev);
    }

    const_reference front() const {
        assert(!empty());
        return *static_cast<const T*>(head.next);
    }

    reference front() {
        assert(!empty());
        return *static_cast<T*>(head.next);
    }

    void Insert(const const_iterator& where, T* node)
    {
        assert(where.node);
        assert(node);
        //assert(contains(where.get()));
        //assert(!contains(node));
        assert(node->prev == nullptr);
        assert(node->next == nullptr);
        auto before = where.node.prev;
        auto after = where.node;
        before->next = node;
        node->prev = before;
        node->next = after;
        after->prev = node;
        ++count;
    }

    bool contains(const T* node) const
    {
        for (auto n = head.next; n != &head; n = n->next)
        {
            if (n == static_cast<const intrusive_list_node*>(node))
                return true;
        }

        return false;
    }

    void remove(T* node) {
        erase({ node });
    }

    void erase(const const_iterator& it)
    {
        assert(it.node);
        //assert(contains(it.get()));
        assert(it.node->prev);
        assert(it.node->next);
        auto node = it.node;
        auto before = node->prev;
        auto after = node->next;
        before->next = after;
        after->prev = before;
        node->prev = nullptr;
        node->next = nullptr;
        --count;
    }

    size_t size() const {
        return count;
    }

    bool empty() const {
        return head.next == &head;
    }

    void clear()
    {
        while(!empty())
            pop_back();
    }

    const_iterator begin() const {
        return { head.next };
    }

    iterator begin() {
        return { head.next };
    }

    const_iterator end() const {
        return { &head };
    }

    iterator end() {
        return { &head };
    }

    const_reverse_iterator rbegin() const {
        return { end() };
    }

    reverse_iterator rbegin() {
        return { end() };
    }

    const_reverse_iterator rend() const {
        return { begin() };
    }

    reverse_iterator rend() {
        return { begin() };
    }
};

} // gc::detail
