#pragma once

#include "common/macro_utility.h"

#include <memory>

#include <zmq.hpp>
namespace mbus
{

    /// rpc 服务的客户端
    class RpcClient
    {
    public:
        DISABLE_COPY_AND_MOVE(RpcClient);

        RpcClient(const std::shared_ptr<zmq::context_t> &ctx);

        /// 开启调试模式，输出调试信息到终端
        void Debug();

        /// 链接代理服务，多次调用会断开连接后重连
        void Connect(const std::string &broker_addr);
        /// 重新连接上一次连接的代理服务
        void Reconnect();

        /// 设置 rpc 请求超时时间
        void SetTimeout(size_t ms);

        /// 设置 rpc 请求失败重试次数
        void SetRerties(size_t rerties);

        /// @brief 发起阻塞等待的 rpc 请求
        /// @param service 目标 rpc 服务的名称
        /// @param argv 请求参数
        std::string SyncCall(const std::string &service, const std::string &argv);

    private:
        std::shared_ptr<zmq::context_t> ctx_{};
        std::unique_ptr<zmq::socket_t> socket_{};
        /// 代理服务的地址
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