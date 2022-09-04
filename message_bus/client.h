#pragma once

#include "common/make_general_message.h"
#include "general_message.pb.h"
#include "message_bus/bus.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include <zmq.hpp>

namespace mbus
{
    value::GeneralMessage Test();

    class Client
    {
        /// 订阅回调函数的类型
        using SubscribeHandle = std::function<void(value::GeneralMessage)>;

    public:
        Client(const std::shared_ptr<zmq::context_t> &ctx);
        ~Client();

        /// @brief 连接总线服务
        /// @param pub_addr 总线服务的订阅端地址，需要符合 zmq 的地址格式
        /// @param sub_addr 总线服务的发布端地址，需要符合 zmq 的地址格式
        /// @thread-safe
        void Connect(const std::string &pub_addr, const std::string &sub_addr);

        /// @brief 连接或开启总线服务，本机通信模式，仅支持本机的多进程或多线程间通信。
        /// 函数调用时会尝试在本机的 pub_port 和 sub_port 端口上开启总线服务，如果失败，
        /// 转换成连接总线服务。
        /// @param pub_port 总线服务的订阅端端口号
        /// @param sub_port 总线服务的发布端端口号
        /// @return 如果本次调用启动了总线服务，返回 true，否则 false
        /// @thread-safe
        bool ConnectOnLocalMode(int32_t pub_port, int32_t sub_port);

        /// @brief 订阅话题
        /// @param topic 需要接收消息的话题
        /// @param handler 收到新消息后的回调函数，传入的第一个参数类型为 GeneralMessage
        /// @note 由于 handler 是在后台线程执行的，用户需要自行保证修改数据时的线程安全问题
        /// @thread-safe
        void Subscribe(std::string topic, SubscribeHandle handler);

        /// @brief 发布消息
        /// @param topic 发布到的话题
        /// @param message 任意类型的消息对象，需要能够被 MakeGeneralMessage() 函数转
        /// 换成通用的 protobuf 消息
        /// @thread-safe
        template <typename MessageType>
        void Publish(const std::string &topic, const MessageType &message)
        {
            /// NOTE: 按话题发送需要使用 zmq 提供的多部分消息。接收部分也同样有相关处理。
            /// http://api.zeromq.org/4-1:zmq-send

            value::GeneralMessage general_message = MakeGeneralMessage(message);
            std::string serialize;
            general_message.SerializeToString(&serialize);

            zmq::message_t topic_part{topic};
            zmq::message_t message_part{std::move(serialize)};
            std::lock_guard<std::mutex> lock(pub_mutex_);

            pub_->send(topic_part, zmq::send_flags::sndmore);
            pub_->send(message_part, zmq::send_flags::none);
            // TODO 错误处理
        }

    private:
        /// 收消息循环
        void ReceiveLoop();

        /// zmq socket 新增话题白名单
        void AddTopic(std::string topic);

        /// zmq socket 移除话题过滤白名单
        void RemoveTopic(std::string topic);

    private:
        /// 全局共享的 zmq IO 事件处理对象
        std::shared_ptr<zmq::context_t> ctx_{nullptr};
        /// 发布用的 socket
        std::mutex pub_mutex_{};
        std::unique_ptr<zmq::socket_t> pub_{nullptr};
        /// 订阅用的 socket
        std::mutex sub_mutex_{};
        std::unique_ptr<zmq::socket_t> sub_{nullptr};
        /// 存储订阅话题的事件处理函数
        std::mutex topic_handlers_mutex_{};
        std::map<std::string, SubscribeHandle> topic_handlers_{};
        /// 存储订阅的话题
        std::set<std::string> subscribed_;
        /// 接收消息的后台工作线程
        std::atomic_bool stop_{false};
        std::thread worker_{};

        /// 本地进程间通信用的总线服务
        std::unique_ptr<Bus> bus_server_{nullptr};
    };

} // namespace mbus