#pragma once

#include <memory>
#include <zmq.hpp>
namespace mbus
{

    /// rpc 服务的客户端
    class RpcClient
    {
    public:
        RpcClient(const RpcClient &) = delete;
        RpcClient(RpcClient &&) = delete;
        RpcClient &operator=(const RpcClient &) = delete;
        RpcClient &operator=(RpcClient &&) = delete;

        RpcClient(const std::shared_ptr<zmq::context_t> &ctx);

        /// 开启调试模式，输出调试信息到终端
        void Debug();

        /// 连接 rpc 服务端
        void Connect(const std::string &broker_addr);

        /// 设置 rpc 请求超时时间
        void SetTimeout(size_t ms);

        /// 设置 rpc 请求失败重试次数
        void SetRerties(size_t rerties);

        /// @brief 发起阻塞等待的 rpc 请求
        /// @param service 目标 rpc 服务的名称
        /// @param argv 请求参数
        std::string SyncCall(const std::string &service, const std::string &argv);

    private:
        /// 等待 zmq::socket 可读事件触发
        bool WaitReadable(zmq::socket_t &socket);

    private:
        std::shared_ptr<zmq::context_t> ctx_{};
        std::unique_ptr<zmq::socket_t> socket_{};
        /// rpc 服务端的地址
        std::string broker_addr_{};
        /// 唯一 id，用于区分身份
        std::string uuid_{};

        /// 请求超时时间，毫秒
        size_t timeout_{2500};
        /// 请求失败重试次数
        size_t retries_{3};
        /// 是否输出调试信息到终端
        bool debug_{};
    };

} // namespace mbus