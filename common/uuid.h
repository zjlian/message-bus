#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>

namespace mbus
{
    /// 获取 16 byte 的 uuid
    inline std::string UUID()
    {
        auto uuid = boost::uuids::random_generator{}();
        return std::string{uuid.begin(), uuid.end()};
    }

    inline std::string UUID2String(const std::string &uuid)
    {
        assert(uuid.size() == 16);
        std::string result{};

        size_t n = 0;
        auto *ptr = result.data();
        for (const auto &c : uuid)
        {
            uint8_t cc = c;
            char s[3];
            sprintf(s, "%02x", cc);
            result += s;
        }
        return result;
    }
} // namespace mbus