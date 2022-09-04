#include "message_bus/client.h"
#include "general_message.pb.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <zmq.hpp>

namespace mbus
{

    value::GeneralMessage Test()
    {
        value::GeneralMessage msg;
        msg.set_payload("Hello World");
        return msg;
    }

    Client::Client(const std::shared_ptr<zmq::context_t> &ctx)
        : ctx_(ctx)
    {
    }

    Client::~Client()
    {
        stop_ = true;
        if (worker_.joinable())
        {
            worker_.join();
        }
    }

    /// @thread-safe
    void Client::Connect(
        const std::string &pub_addr, const std::string &sub_addr)
    {
        std::lock_guard<std::mutex> lock1(pub_mutex_);
        std::lock_guard<std::mutex> lock2(sub_mutex_);

        assert(!worker_.joinable());

        pub_ = std::make_unique<zmq::socket_t>(*ctx_, zmq::socket_type::pub);
        assert(pub_ != nullptr && "Out Of Memory!!");
        std::cout << "发布端连接 " << pub_addr << std::endl;
        pub_->connect(pub_addr);

        sub_ = std::make_unique<zmq::socket_t>(*ctx_, zmq::socket_type::sub);
        assert(sub_ != nullptr && "Out Of Memory!!");
        std::cout << "订阅端连接 " << sub_addr << std::endl;
        sub_->connect(sub_addr);

        // 创建后台接收线程
        worker_ = std::thread{[&] {
            ReceiveLoop();
        }};
    }

    /// @thread-safe
    bool Client::ConnectOnLocalMode(int32_t pub_port, int32_t sub_port)
    {
        auto localhost = std::string{"tcp://localhost:"};
        auto pub_addr = localhost + std::to_string(pub_port);
        auto sub_addr = localhost + std::to_string(sub_port);
        Connect(pub_addr, sub_addr);
        return true;
    }

    /// @thread-safe
    void Client::Subscribe(std::string topic, SubscribeHandle handler)
    {
        // zmq socket 添加话题过滤必须在保存回调函数之后，
        // 以保证收到新话题的消息时回调函数是存在的。
        {
            std::lock_guard<std::mutex> lock1(topic_handlers_mutex_);
            // 先存一份话题字符串的拷贝
            auto [iterator, success] = subscribed_.emplace(topic);
            auto &view = *iterator;
            assert(view == topic && "std::set::emplace 的返回值不是插入的元素");
            // 然后用 subscribed_ 内话题字符串的 string_view 映射回调函数
            topic_handlers_[view] = std::move(handler);
        }
        // FIXME: 暂不清楚 zmq_setsockopt 是否线程安全，官方文档没看到相关说明，
        // 但是在 libzmq 的 github issues 上找到一个 zmq 开发者发布的贴子说是安全的
        // https://github.com/zeromq/libzmq/issues/3817
        AddTopic(topic);
    }

    /// 收消息循环
    void Client::ReceiveLoop()
    {
        // NOTE: 按话题接收需要使用 zmq 的多部分消息处理
        // 参考 http://api.zeromq.org/4-1:zmq-recv

        while (!stop_)
        {
            std::vector<zmq::message_t> message;
            std::unique_lock<std::mutex> lock1(sub_mutex_);

            do
            {
                zmq::message_t msg;
                auto result = sub_->recv(msg, zmq::recv_flags::none);
                if (result.has_value())
                {
                    message.emplace_back(std::move(msg));
                }
            } while (sub_->get(zmq::sockopt::rcvmore));
            lock1.unlock();

            if (message.size() <= 1)
            {
                // TODO: 处理收到没有按话题发布或是超时之类的异常
                continue;
            }

            // 检查话题是否有注册回调函数
            auto &topic = message[0];
            // std::string_view topic_sv{
            //     static_cast<char *>(topic.data()), topic.size()};
            std::string topic_str{
                static_cast<char *>(topic.data()), topic.size()};
            // std::cout << topic_sv << std::endl;
            // 序列化回 proto 对象
            auto &msg = message[1];
            value::GeneralMessage general_message;
            if (!msg.empty())
            {
                general_message.ParseFromArray(msg.data(), msg.size());
            }

            // 调用对应的回调函数
            std::unique_lock<std::mutex> lock2(topic_handlers_mutex_);
            auto iterator = topic_handlers_.find(topic_str);
            assert(iterator != topic_handlers_.end() && "收到未注册的话题");
            iterator->second(std::move(general_message));
        }
    }

    /// zmq socket 新增话题白名单
    void Client::AddTopic(std::string topic)
    {
        assert(sub_ != nullptr);
        sub_->set(zmq::sockopt::subscribe, topic);
    }

    /// zmq socket 移除话题过滤白名单
    void Client::RemoveTopic(std::string topic)
    {
        assert(sub_ != nullptr);
        sub_->set(zmq::sockopt::unsubscribe, topic);
        // subscribed_.erase(std::string{topic});
    }
} // namespace mbus