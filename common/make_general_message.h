#pragma once

#include "general_message.pb.h"

#include <google/protobuf/message_lite.h>
#include <type_traits>

namespace mbus
{
    ///
    /// NOTE:
    /// 类型特征识别 std::is_convertible，判断某个类型是否能转换到另一个类型
    /// https://zh.cppreference.com/w/cpp/types/is_convertible
    ///
    /// std::is_base_of，判断某个类型是否是指定类型的派生类
    /// https://zh.cppreference.com/w/cpp/types/is_base_of
    ///

    /// 任何可转换成 std::string 的数据类型，都生成 RAW 协议的通用消息
    template <typename MessageType>
    auto MakeGeneralMessage(const MessageType &msg)
        -> typename std::enable_if<
            std::is_convertible<MessageType, std::string>::value,
            value::GeneralMessage>::type
    {
        value::GeneralMessage result;
        result.set_not_timeout(true);
        result.set_protocal(value::PAYLOAD_PROTOCAL_RAW);
        result.set_payload(std::string{msg});

        return result;
    }

    /// 任何派生自 google::protobuf::MessageLite 的类，都生成 PROTO 协议的通用消息
    template <typename MessageType>
    auto MakeGeneralMessage(const MessageType &msg)
        -> typename std::enable_if<
            std::is_base_of<google::protobuf::MessageLite, MessageType>::value,
            value::GeneralMessage>::type
    {
        value::GeneralMessage result;
        result.set_not_timeout(true);
        // 序列化到 std::string
        std::string buffer;
        msg.SerializeToString(&buffer);
        // 填入通用消息体内
        result.set_protocal(value::PAYLOAD_PROTOCAL_PROTO);
        result.set_payload(std::move(buffer));

        return result;
    }

    /* 下面是解析通用消息获取指定类型的 payload 的实现 */

    /// 解析通用消息的 payload 部分到 std::string 上，返回解析后的 std::string 实例
    /// 只有 MessageType 指定的类型为 std::string，才会重载到这个函数
    template <typename MessageType>
    auto UnmakeGeneralMessage(const value::GeneralMessage &msg)
        -> typename std::enable_if<
            std::is_same<MessageType, std::string>::value,
            std::string>::type
    {
        return msg.payload();
    }

    /// 解析通用消息的 payload 部分到各个具体的 protobuf 序列化对象上，
    /// 只有 MessageType 指定的类型继承于 google::protobuf::MessageLite，才会重载到这个函数
    template <typename MessageType>
    auto UnmakeGeneralMessage(const value::GeneralMessage &msg)
        -> typename std::enable_if<
            std::is_base_of<google::protobuf::MessageLite, MessageType>::value,
            MessageType>::type
    {
        MessageType result;
        if (!msg.payload().empty())
        {
            result.ParseFromString(msg.payload());
        }
        return result;
    }

} // namespace mbus
