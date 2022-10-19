#pragma once

#include <cassert>
#include <cstring>
#include <deque>
#include <iostream>
#include <vector>

#include <zmq.hpp>

namespace mbus
{

    using MessagePack = std::deque<zmq::message_t>;
    using BufferPack = std::deque<zmq::const_buffer>;

    /// 比较是否相等
    inline bool Equal(const zmq::message_t &msg, const std::string &str)
    {
        return memcmp(msg.data<char>(), str.data(), msg.size()) == 0;
    }

    /// 转换 std::string 到 zmq::const_buffer
    inline zmq::const_buffer MakeZmqBuffer(const std::string &str)
    {
        return zmq::const_buffer{str.data(), str.size()};
    }

    /// 转换 std::string 到 zmq::messages_t
    inline zmq::message_t MakeMessage(const std::string &str)
    {
        return zmq::message_t{str};
    }

    inline zmq::const_buffer MakeZmqBuffer(const char *str)
    {
        return zmq::const_buffer{str, strlen(str)};
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
        std::cout << StringifyMessages(messages) << std::endl;
        auto remain = messages.size();

        for (auto &msg : messages)
        {
            auto flags = zmq::send_flags::none;
            if (remain > 1)
            {
                flags = zmq::send_flags::sndmore;
            }
            std::cout << static_cast<int32_t>(flags) << std::endl;
            auto result = socket.send(msg, flags);
            if (!result.has_value())
            {
                return false;
            }
            remain--;
        }

        return true;
    }

    /// 等待 zmq socket 可读
    inline bool WaitReadable(zmq::socket_t &socket, int32_t timeout)
    {
        std::array<zmq::pollitem_t, 1> items{{socket.handle(), 0, ZMQ_POLLIN, 0}};
        zmq::poll(items, std::chrono::milliseconds(timeout));
        return items[0].revents & ZMQ_POLLIN;
    }

} // namespace mbus