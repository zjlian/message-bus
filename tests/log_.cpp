#include "general_message.pb.h"
#include "message_bus/async_log.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <ostream>
#include <thread>
#include <zmq.hpp>

int main()
{
    auto &log_entity = mbus::AsyncLog::GetInstance();
    auto ctx = std::make_shared<zmq::context_t>(1);
    auto log = std::make_unique<zmq::socket_t>(*ctx, zmq::socket_type::pair);
    log->connect("ipc://log");

    value::GeneralMessage general_message_to_read;
    while (true)
    {
        // while (true)
        // {
        //     std::this_thread::sleep_for(std::chrono::milliseconds(500));
        //     logEntity.ParseMessage();
        // }
        std::vector<zmq::message_t> message;
        do
        {
            zmq::message_t msg;
            // std::cout << "recv begin" << std::endl;
            auto result = log->recv(msg, zmq::recv_flags::none);
            // std::cout << "recv end" << std::endl;
            if (result.has_value())
            {
                message.emplace_back(std::move(msg));
            }
        } while (log->get(zmq::sockopt::rcvmore));

        if (message.size() <= 1)
        {
            // TODO: 处理收到没有按话题发布或是超时之类的异常
            continue;
        }

        // 检查话题是否有注册回调函数
        auto &topic = message[0];
        std::string_view topic_sv{
            static_cast<char *>(topic.data()), topic.size()};
        // 序列化回 proto 对象
        auto &msg = message[1];
        value::GeneralMessage general_message;
        if (!msg.empty())
        {
            general_message.ParseFromArray(msg.data(), msg.size());
        }
        if (!std::filesystem::is_directory(topic_sv) || !std::filesystem::exists(topic_sv))
        {
            auto rp = std::filesystem::create_directory(topic_sv);
            assert(rp);
        }
        std::cout << general_message.payload() << std::endl;

        log_entity.log(std::string{topic_sv}, general_message);
    }
}