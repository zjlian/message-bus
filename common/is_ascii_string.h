#pragma once

#include <cstddef>

namespace mbus
{
    /// 判断二进制内容是否为 ASCII 字符串
    inline bool IsASCII(const void *ptr, size_t size)
    {
        if (size == 0)
        {
            return false;
        }

        const char *str = static_cast<const char *>(ptr);

        size_t text_count = 0;

        for (size_t i = 0; i < size; i++)
        {
            if ((str[i] >= 0x20 && str[i] <= 0x7f) ||
                str[i] == '\n' ||
                str[i] == '\r' ||
                str[i] == '\t')
            {
                text_count++;
            }
        }

        double text_percent = static_cast<double>(text_count) / static_cast<double>(size);
        // 90% 以上的 ASCII 字符，就认为是 ASCII 字符串
        return text_percent >= 0.9;
    }
} // namespace mbus