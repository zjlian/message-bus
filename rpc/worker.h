#pragma once

#include "common/macro_utility.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include <zmq.hpp>

namespace mbus
{

    /// rpc 服务提供端口，客户端请求的服务，最终都由 RpcWorker 实例处理
    class RpcWorker
    {
    public:
        DISABLE_COPY_AND_MOVE(RpcWorker);

        RpcWorker(const std::shared_ptr<zmq::context_t> &ctx);
        ~RpcWorker();

        void Debug();

        /// 响应 rpc 请求的回调函数签名，第一个是请求携带的参数
        using ServiceCallback = std::string(const std::string &, const int64_t &);
        /// 向代理端注册想要接收的服务请求，需要在 Connect 之前调用
        void RegisterService(const std::string &service_name, const std::function<ServiceCallback> &callback);

        /// 链接代理服务，多次调用会断开连接后重连
        void Connect(const std::string &broker_addr);
        /// 重新连接上一次连接的代理服务
        void Reconnect();
        /// 下线
        void Deconnect();

        /// 设置信号检测延迟
        void SetHeartbeat(int32_t heartbeat);

        /// 设置重连延迟
        void SetReconnectDelay(int32_t delay);

    private:
        /// 响应请求，client_uuid 为目标客户端的标识符，body 为响应的内容
        bool Response(const std::string &client_uuid, const std::string &serial_num, const std::string &service_name, const std::string &body);

        /// 发送消息到代理端
        void SendToBroker(const std::string &header, const std::string &command, const std::string &body);

        /// 循环监听任务
        void ReceiveLoop();

        /// 消息处理
        void HandleMessage();

    private:
        std::atomic<bool> stop_{true};
        std::shared_ptr<zmq::context_t> ctx_{};
        /// 代理服务的地址
        std::string broker_addr_{};
        /// 该 rpc worker 提供的服务名称
        std::mutex services_mutex_{};
        std::unordered_map<std::string, std::function<ServiceCallback>> services_{};
        /// 该 rpc worker 的表示符号
        std::string uuid_{};
        std::unique_ptr<zmq::socket_t> socket_{};
        std::atomic<bool> debug_{};

        /// 收到到所有消息的队列
        std::mutex mx_task_one_;
        std::deque<std::deque<zmq::message_t>> messages_task_one_{};
        std::mutex mx_task_two_;
        std::deque<std::deque<zmq::message_t>> messages_task_two_{};
        /// 执行注册函数后到返回结果
        uint64_t current_max_serial_num_of_request{0};

        // 心跳包发送时间
        uint64_t heartbeat_at_{};
        size_t liveness_{};
        // 超时时间
        int32_t heartbeat_{2000};
        int32_t reconnect_delay_{2000};

        int32_t expect_reply{};

        // 工作线程
        std::thread worker_{};
        std::thread handler_{};

        // 同步发送互斥量
        std::mutex send_mx_;
    };

} // namespace mbus