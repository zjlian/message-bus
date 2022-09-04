#pragma once

#include <cassert>
#include <deque>
#include <vector>
#include <zmq.hpp>

namespace mbus
{

    /// 转换 std::string 到 zmq::const_buffer
    inline zmq::const_buffer MakeZmqBuffer(const std::string &str)
    {
        return zmq::const_buffer{str.data(), str.size()};
    }

    /// 接收 zmq 的全部分块消息
    inline std::deque<zmq::message_t> ReceiveAll(zmq::socket_t &socket)
    {
        std::deque<zmq::message_t> message;
        do
        {
            zmq::message_t msg;
            auto result = socket.recv(msg, zmq::recv_flags::none);
            if (result.has_value())
            {
                message.emplace_back(std::move(msg));
            }
        } while (socket.get(zmq::sockopt::rcvmore));

        return message;
    }

    /// 发送全部消息
    template <typename ListType>
    inline bool SendAll(zmq::socket_t &socket, ListType &messages)
    {
        auto remain = messages.size();

        for (auto &msg : messages)
        {
            auto flags = zmq::send_flags::none;
            if (remain > 1)
            {
                flags = zmq::send_flags::sndmore;
            }

            auto result = socket.send(msg, flags);
            if (!result.has_value())
            {
                return false;
            }
            remain--;
        }

        return true;
    }

    /// 将消息集合转换成字符串
    template <typename ListType>
    inline std::string StringifyMessages(const ListType &msgs)
    {
        std::string result;
        for (const auto &m : msgs)
        {
            result.append(static_cast<const char *>(m.data()), m.size());
            result += "; ";
        }

        return result;
    }
} // namespace mbus