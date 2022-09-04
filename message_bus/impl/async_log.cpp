#include "message_bus/async_log.h"
#include "general_message.pb.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <fstream>
#include <ios>
#include <iostream>
#include <memory>
#include <mutex>
#include <ostream>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <utility>

#include <filesystem>

namespace mbus
{

    AsyncLog::AsyncLog()
    {
        worker_ = std::thread{[&] {
            LogLoop();
        }};
    }

    AsyncLog::~AsyncLog()
    {
        if (worker_.joinable())
        {
            to_run_.store(false);
            worker_.join();
        }
    }

    void AsyncLog::SerializeMessage(BufferNode &node)
    {
        // 暂时一次持久化一个节点
        auto msg = node.buff.front();
        char buf[msg.ByteSizeLong()];
        auto flag = msg.SerializeToArray(buf, msg.ByteSizeLong());
        node.out.put(msg.ByteSizeLong());
        node.out.write(buf, msg.ByteSizeLong());
        // todo flush可以优化
        node.out.flush();
        node.buff.pop_front();
        std::cout << "buff size is " << node.buff.size() << std::endl;
    }

    void AsyncLog::ParseMessage()
    {
        std::ifstream file2(std::filesystem::current_path() / "msg" / "hello", std::ios::binary);
        value::GeneralMessage general_message_to_read;
        while (file2.peek() != EOF)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            auto size = file2.get();
            char buf[size];
            file2.read(buf, size);
            general_message_to_read.ParseFromArray(buf, size);
            std::cout << "序列化了：" << general_message_to_read.payload() << std::endl;
        }
    }

    // @thread-safe
    void AsyncLog::log(const std::string &topic, const value::GeneralMessage &msg)
    {
        auto search = topic_map_.find(topic);
        if (search == topic_map_.end())
        {
            // 提醒另一条工作线程释放锁
            to_add_topic.store(true);
            // 如果主题队列中不存在该主题，则进行添加
            std::unique_lock<std::mutex> map_lock(map_mx_);
            // 再查询一遍并判断是否存在
            if ((search = topic_map_.find(topic)) == topic_map_.end())
            {
                BufferNode node;
                node.topic = topic;
                node.mx = std::make_unique<std::mutex>();
                node.out = std::ofstream(std::filesystem::current_path() / PATH / topic, std::ios::app | std::ios::binary);
                topic_map_.emplace(topic, std::move(node));
                search = topic_map_.find(topic);
                std::cout << "insert" << std::endl;
            }
            to_add_topic.store(false);
        }
        std::lock_guard<std::mutex> lock(*search->second.mx.get());
        search->second.buff.push_back(std::move(msg));
    }

    // @thread-safe
    void AsyncLog::LogLoop()
    {
        const auto p = std::filesystem::current_path() / PATH;
        if (!std::filesystem::exists(p))
        {
            auto c = std::filesystem::create_directory(p);
        }

        while (to_run_.load())
        {
            // std::this_thread::sleep_for(std::chrono::milliseconds(10000));
            std::unique_lock<std::mutex> map_lock(map_mx_);
            if (!map_lock.owns_lock())
                continue;

            for (auto begin = topic_map_.begin(); begin != topic_map_.end(); begin++)
            {
                std::unique_lock<std::mutex> try_lock(*begin->second.mx.get(), std::try_to_lock);
                if (!try_lock.owns_lock())
                {
                    // 获取不到锁说明当前节点被其他线程持有
                    continue;
                }
                if (begin->second.buff.empty())
                {
                    // 如果为空则跳过
                    continue;
                }
                if (to_add_topic.load())
                {
                    // 如果其他线程要求存入新的topic，则释放锁
                    std::cout << "unlock" << std::endl;
                    break;
                }
                // 序列化消息
                SerializeMessage(begin->second);
            }
        }
    }

} // namespace mbus