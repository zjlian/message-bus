#pragma once

#include <cstdint>
#include <ctime>

#include <sys/time.h>

namespace mbus
{

    /// 获取当前系统时间的毫秒数
    inline int64_t Now()
    {
        timeval tv;
        gettimeofday(&tv, nullptr);
        return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
    }

} // namespace mbus