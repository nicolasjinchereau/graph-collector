/*--------------------------------------------------------------*
*  Copyright (c) 2022 Nicolas Jinchereau. All rights reserved.  *
*--------------------------------------------------------------*/

#include <gc/gc.h>
#include <future>
#include <numeric>

using namespace std::chrono;

namespace gc {

graph* graph::That::operator->() {
    static graph pg;
    return &pg;
}

graph::That graph::that;

graph::graph()
{
    ranges.reserve(100'000);
    rngs.reserve(100'000);
    info.reserve(100'000);
    scan.reserve(100'000);
    keep.reserve(100'000);
}

graph::~graph()
{
    // orphan attached pointers
    // leak uncollected cycles
    // leak raw memory
}

void graph::attach(graph_ptr<void>* gp)
{
    std::lock_guard lk(graphLock);
    pointers.push_back(const_cast<graph_ptr<void>*>(gp));
}

void graph::detach(graph_ptr<void>* gp)
{
    std::lock_guard lk(graphLock);
    pointers.remove(const_cast<graph_ptr<void>*>(gp));
}

void graph::attach(raw_graph_ptr<void>* gp)
{
    std::lock_guard lk(graphLock);
    rawPointers.push_back(const_cast<raw_graph_ptr<void>*>(gp));
}

void graph::detach(raw_graph_ptr<void>* gp)
{
    std::lock_guard lk(graphLock);
    rawPointers.remove(const_cast<raw_graph_ptr<void>*>(gp));
}

void graph::add_range(void* p, size_t size)
{
    std::lock_guard lk(graphLock);

    std::byte* bp = static_cast<std::byte*>(p);

    auto it = std::upper_bound(ranges.begin(), ranges.end(), bp,
        [](std::byte* p, const memory_range& r) { return p < r.begin; });

    ranges.insert(it, memory_range{ bp, bp + size });
}

void graph::remove_range(void* p)
{
    std::lock_guard lk(graphLock);

    auto it = find_range_iterator(p);
    assert(it != ranges.end());
    ranges.erase(it);
}

garbage graph::collect() {
    return that->collect_impl();
}

garbage graph::collect_impl()
{
    if (that->collecting.exchange(true)) {
        printf("collection already in progress\n");
        return garbage();
    }

    auto start = std::chrono::steady_clock::now();

    size_t managedPointerCount = 0;
    
    {
        std::scoped_lock lk(pointerLock, graphLock);

        size_t totalPointers = pointers.size() + rawPointers.size();
        info.reserve(totalPointers);
        scan.reserve(totalPointers);
        keep.reserve(totalPointers);

        rngs.reserve(ranges.size());
        for(auto& r : ranges)
            rngs.push_back({ r.begin, r.end, false, false });

        for (auto& gp : pointers)
        {
            if (gp)
            {
                auto it_r = find_range_iterator(gp.get());
                size_t idx_r = static_cast<size_t>(it_r - ranges.begin());

                scan_info si;
                si.gp = &gp;
                si.range = &rngs[idx_r];
                si.range->managed = true;

                uint32_t idx = (uint32_t)info.size();
                info.push_back(si);

                if (bool isRoot = !find_range(si.gp))
                    keep.push_back(idx);
                else
                    scan.push_back(idx);

                ++managedPointerCount;
            }
        }

        for (auto& rgp : rawPointers)
        {
            auto it_r = find_range_iterator(rgp.ptr);
            if (it_r != ranges.end())
            {
                size_t idx_r = static_cast<size_t>(it_r - ranges.begin());

                scan_info si;
                si.gp = &rgp;
                si.range = &rngs[idx_r];
                si.range->managed = false;

                uint32_t idx = (uint32_t)info.size();
                info.push_back(si);

                if (bool isRoot = !find_range(si.gp))
                    keep.push_back(idx);
                else
                    scan.push_back(idx);
            }
        }
    } // scoped_lock

    detail::vector<std::shared_ptr<void>> unreachable;
    unreachable.reserve(managedPointerCount);

    for (size_t i = 0; i != keep.size(); ++i)
    {
        scan_info& parent = info[keep[i]];

        if(parent.range->scanned)
            continue;

        for(auto it = scan.begin(); it != scan.end(); )
        {
            auto idx = *it;

            std::byte* bp = static_cast<std::byte*>(info[idx].gp);

            if (bp >= parent.range->begin && bp < parent.range->end)
            {
                keep.push_back(idx);
                *it = scan.back();
                scan.pop_back();
            }
            else
            {
                ++it;
            }
        }

        parent.range->scanned = true;
    }

    for (auto& idx : scan)
    {
        scan_info& si = info[idx];

        if (si.range->managed)
        {
            auto mptr = static_cast<graph_ptr<void>*>(si.gp);
            unreachable.push_back(std::move(mptr->ptr));
        }
    }

    rngs.clear();
    info.clear();
    scan.clear();
    keep.clear();

    collecting = false;

    size_t objCount = unreachable.size();

    auto dur = std::chrono::steady_clock::now() - start;
    float seconds = std::chrono::duration_cast<std::chrono::duration<float>>(dur).count();
    printf("Collected %d objects in %f seconds\n", (int)objCount, seconds);

    return garbage(std::move(unreachable));
}

int graph::allocated_objects()
{
    std::lock_guard lk(that->graphLock);
    return (int)that->ranges.size();
}

size_t graph::allocated_bytes()
{
    std::lock_guard lk(that->graphLock);
    
    size_t total = 0;
    
    for (auto& range : that->ranges)
        total += (range.end - range.begin);

    return total;
}

std::optional<memory_range> graph::find_range(void* internalPtr)
{
    std::optional<memory_range> ret;

    auto it = find_range_iterator(internalPtr);
    if (it != ranges.end())
        ret = *it;

    return ret;
}

detail::vector<memory_range>::iterator graph::find_range_iterator(void* internalPtr)
{
    if (internalPtr && !ranges.empty())
    {
        std::byte* bp = static_cast<std::byte*>(internalPtr);

        std::byte* allBegin = ranges.front().begin;
        std::byte* allEnd = ranges.back().end;

        if (bp >= allBegin && bp <= allEnd)
        {
            auto it = std::upper_bound(ranges.begin(), ranges.end(), bp,
                [](std::byte* p, const memory_range& r) { return p < r.begin; });

            if (it != ranges.begin())
            {
                --it;

                if (bp >= it->begin && bp <= it->end) {
                    return it;
                }
            }
        }
    }

    return ranges.end();
}

} // gc
