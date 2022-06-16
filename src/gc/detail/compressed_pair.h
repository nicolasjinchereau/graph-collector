/*--------------------------------------------------------------*
*  Copyright (c) 2022 Nicolas Jinchereau. All rights reserved.  *
*--------------------------------------------------------------*/

#pragma once
#include <type_traits>

namespace gc::detail {

template<class T1, class T2, bool = std::is_empty_v<T1> && !std::is_final_v<T1>>
class compressed_pair final : protected T1
{
public:
    T2 val2;

    compressed_pair()
        : T1(), val2() {}

    template<class V1, class V2>
    compressed_pair(V1&& v1, V2&& v2)
        : T1(std::forward<V1>(v1)), val2(std::forward<V2>(v2)) {}

    T1& first() {
        return *this;
    }

    T2& second() {
        return val2;
    }
};

template<class T1, class T2>
class compressed_pair<T1, T2, false> final
{
public:
    T1 val1;
    T2 val2;

    compressed_pair()
        : val1(), val2() {}

    template<class V1, class V2>
    compressed_pair(V1&& v1, V2&& v2)
        : val1(std::forward<V1>(v1)), val2(std::forward<V2>(v2)) {}

    T1& first() {
        return val1;
    }

    T2& second() {
        return val2;
    }
};

} // gc::detail
