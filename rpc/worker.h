#pragma once

#include "common/macro_utility.h"

#include <atomic>
#include <cstdint>
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
        using ServiceCallback = std::string(const std::string &);
        /// 向代理端注册想要接收的服务请求，需要在 Connect 之前调用
        void RegisterService(const std::string &service_name, const std::function<ServiceCallback> &callback);

        /// 链接代理服务，多次调用会断开连接后重连
        void Connect(const std::string &broker_addr);
        /// 重新连接上一次连接的代理服务
        void Reconnect();

        /// 设置信号检测延迟
        void SetHeartbeat(int32_t heartbeat);

        /// 设置重连延迟
        void SetReconnectDelay(int32_t delay);

    private:
        /// 响应请求，client_uuid 为目标客户端的标识符，body 为响应的内容
        void Response(const std::string &client_uuid, const std::string &service_name, const std::string &body);

        /// 发送消息到代理端
        void SendToBroker(const std::string &header, const std::string &command, const std::string &body);

        /// 工作循环
        void ReceiveLoop();

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

        // 心跳包发送时间
        uint64_t heartbeat_at_{};
        size_t liveness_{};
        // 超时时间
        int32_t heartbeat_{2500};
        int32_t reconnect_delay_{2500};

        int32_t expect_reply{};

        // 工作线程
        std::thread worker_{};
    };

} // namespace mbus