#pragma once
#ifndef VPSK_COMMAND_SORT_KEY_HPP
#define VPSK_COMMAND_SORT_KEY_HPP
#include <cstdint>

namespace vpsk {

    using cmd_sort_key_t = uint64_t;
    /*
    MSB 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 LSB
        ^ ^       ^  ^                                   ^^                 ^
        | |       |  |                                   ||                 |- 3 bits - Shader System (Pass Immediate)
        | |       |  |                                   ||- 16 bits - Depth
        | |       |  |                                   |- 1 bit - Instance bit
        | |       |  |- 32 bits - User defined
        | |       |- 3 bits - Shader System (Pass Deferred)
        | - 7 bits - Layer System
        |- 2 bits - Unused
    from: http://bitsquid.blogspot.com/2017/02/stingray-renderer-walkthrough-4-sorting.html

    */

}

#endif //!VPSK_COMMAND_SORT_KEY_HPP
