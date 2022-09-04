#pragma once

#include "common/make_general_message.h"
#include "general_message.pb.h"
#include "message_bus/client.h"

#include <memory>

#include <zmq.hpp>

namespace mbus
{

    /**
     * 消息订阅中心
    */
    class MessageDealer
    {
    public:
        /// 禁止复制和移动
        MessageDealer(const MessageDealer &) = delete;
        MessageDealer(MessageDealer &&) = delete;
        MessageDealer &operator=(const MessageDealer &) = delete;
        MessageDealer &operator=(MessageDealer &&) = delete;

        /// 构造函数，需要提供一个 zmq context 实例
        MessageDealer(const std::shared_ptr<zmq::context_t> &ctx)
            : ctx_(ctx), mbus_client_(ctx_)
        {
        }

        /// 本机自动连接模式
        void Connect(int32_t pub_port, int32_t sub_port)
        {
            mbus_client_.ConnectOnLocalMode(pub_port, sub_port);
        }

        /// 指定消息总线服务的地址进行连接
        void Connect(const std::string &pub_addr, const std::string &sub_addr)
        {
            mbus_client_.Connect(pub_addr, sub_addr);
        }

        /**
         * @brief 添加新的话题订阅回调，同一个话题可以添加多个
         * @param MessageType 模板参数，用于指定接收消息后需要解析的类型
         * @param topic 订阅的话题
         * @param callback 消息处理回调函数，第一个参数是预期接收的消息类型，第二个是原始的
         *                 通用消息
         * @thread-safe
        */
        template <typename MessageType>
        void AddTopicListener(
            const std::string &topic,
            const std::function<void(MessageType, value::GeneralMessage)> &callback)
        {
            // mbus::Client 的回调函数只支持接收一个 value::GeneralMessage 参数，
            // 所以必须要额外包装一层 lambda 给到 mbus::Client 订阅话题。
            auto unpack_wrapper = [callback](value::GeneralMessage msg) {
                // 提取通用消息包中的 payload，并且反序列化到 MessageType 类型上
                auto payload_msg = UnmakeGeneralMessage<MessageType>(msg);
                // 因为 msg 是值传递的，直接用 move 转移所有权减少复制，target_msg 同理
                callback(std::move(payload_msg), std::move(msg));
            };

            mbus_client_.Subscribe(topic, std::move(unpack_wrapper));
        }

        /**
         * @brief 发布新消息到指定话题里
         * @param MessageType 发布的消息类型，可省略不写，让函数模板自动推导
         * @param topic 发布的目标话题
         * @param msg 发布的消息
        */
        template <typename MessageType>
        void Publish(const std::string &topic, const MessageType &msg)
        {
            mbus_client_.Publish(topic, msg);
        }

    private:
        // zmq context
        std::shared_ptr<zmq::context_t> ctx_;
        // 消息总线服务的客户端
        mbus::Client mbus_client_;
    };

} // namespace mbus
