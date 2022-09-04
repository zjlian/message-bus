#pragma once

#include <memory>
#include <thread>

#include <zmq.hpp>

namespace mbus
{
    /// 总线服务的实现类
    class Bus
    {
    public:
        Bus(const std::shared_ptr<zmq::context_t> &ctx);
        ~Bus();

        /// 启动服务
        /// @param host 总线服务的地址
        /// @param xpub_port 总线服务的输出端端口号
        /// @param xsub_port 总线服务的输入端端口号
        void Run(std::string host, int32_t xpub_port, int32_t xsub_port);

        /// 尝试启动仅支持本机进程间通信的服务
        bool TryRunOnLocalMode(
            int32_t xpub_port, int32_t xsub_port);

    private:
        /// 初始化
        /// @param host 总线服务的地址
        /// @param xpub_port 总线服务的输出端端口号
        /// @param xsub_port 总线服务的输入端端口号
        void Bind(std::string host, int32_t xpub_port, int32_t xsub_port);

        /// 尝试启动仅支持本机进程间通信的服务
        /// @param host 总线服务的地址
        /// @param xpub_port 总线服务的输出端端口号
        /// @param xsub_port 总线服务的输入端端口号
        bool TryBindOnLocalMode(int32_t xpub_port, int32_t xsub_port);

    private:
        std::shared_ptr<zmq::context_t> ctx_;
        std::unique_ptr<zmq::socket_t> backend_;
        std::unique_ptr<zmq::socket_t> frontend_;
        std::unique_ptr<zmq::socket_t> log_;
        std::thread worker_;
    };

} // namespace mbus