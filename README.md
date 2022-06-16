# Graph Collector

This library provides localized garbage collection for C++ while preserving the option for RAII.

## `gc::graph_ptr`

This class builds on `std::shared_ptr` by adding reachability tracing so that cycles can be detected and their associated pointers can be reset. Cycles can still be broken using `gc::weak_graph_ptr`, but it is not required. Cycles can also be collected by calling `gc::graph::collect` if they become unreachable. For objects not involved in a cycle, ref-counting works as one would expect from `std::shared_ptr`, and destructors are called when all references have gone out of scope.

## `gc::weak_graph_ptr`

Weak pointers can be used to break cycles, but they can also be used for data members that need to be accessed from the destructor of their parent object. Since weak pointers don't own the object they reference, they are excluded from collection, and won't be reset by calls to `gc::graph::collect`.

## `gc::allocator` and `gc::raw_graph_ptr`

A call to `gc::allocator::allocate` will return a `gc::raw_graph_ptr` to the allocated memory, which must be passed to `gc::allocator::deallocate` in order to be freed. Instances of `gc::raw_graph_ptr` don't count references, and won't be reset by `gc::graph::collect`, but they do participate in reachability tracing.

Typedefs of some standard containers are provided, such as `gc::vector`, that use `gc::allocator` to manage their memory and enable reachability tracing of their contents.

## `gc::graph_object`

This is effectively the same thing as `std::enable_shared_from_this`. Currently, the caller must provide a `this` pointer to `gc::graph_object::self(T*)`, but that requirement will be dropped once C++23's [explicit object parameter](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html) becomes widely supported.

## `gc::graph`

This class contains the bookkeeping and collection logic associated with `gc::graph_ptr` and `gc::raw_graph_ptr`. Those two pointer types will register/unregister themselves with `gc::graph` upon construction/destruction to allow reachability tracing.

This class is thread-safe, and `gc::graph::collect` can be called on a background thread to avoid blocking the UI thread. Calls to `gc::graph::collect` will return a `garbage` object that can be moved back to the UI thread to be destroyed if the application requires it.

All pointers must remain unchanged during reachability tracing. This is achieved by use of `std::shared_mutex` to implement a sort of stop-the-world mechanic. All mutations of `gc::graph_ptr` and `gc::raw_graph_ptr` are gaurded by the shared lock. When a collection is started, `gc::graph` will aquire the exclusive lock until the current state of the graph has been captured.

## Performance

This library has been tested with a very basic game, and performs collections routinely without any noticeable stalls on the UI thread. Collection is a little slow, and may lead to unacceptable delays in destruction at scale, but the collection implementation is very basic with a lot of room for improvement.

The aformentioned test game can be found here:<br>
https://github.com/nicolasjinchereau/microwave

## Acknowledgements

Thanks to Herb Sutter for his presentations at CppCon, and for sharing his project  [gcpp](https://github.com/hsutter/gcpp/), which was the inspiration for this library.
