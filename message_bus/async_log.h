#pragma once

#include "general_message.pb.h"

#include <atomic>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
namespace mbus
{

    struct BufferNode
    {
        std::deque<value::GeneralMessage> buff;
        std::unique_ptr<std::mutex> mx;
        std::string topic;
        std::ofstream out;
    };
    class AsyncLog
    {
    public:
        static AsyncLog &GetInstance();
        AsyncLog(const AsyncLog &) = delete;
        AsyncLog(AsyncLog &&) = delete;
        AsyncLog &operator=(const AsyncLog &) = delete;
        AsyncLog &operator=(AsyncLog &&) = delete;
        ~AsyncLog();

        void log(const std::string &, const value::GeneralMessage &);
        void ParseMessage();

    private:
        AsyncLog();
        void LogLoop();
        void SerializeMessage(BufferNode &);

    private:
        const std::string PATH = "msg";
        std::map<std::string, BufferNode> topic_map_;
        std::thread worker_{};
        std::mutex map_mx_;
        std::atomic_bool to_run_{true};
        std::atomic_bool to_add_topic{false};
    };

    inline AsyncLog &AsyncLog::GetInstance()
    {
        static AsyncLog asyncLog;
        return asyncLog;
    }
} // namespace mbus