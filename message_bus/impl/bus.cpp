#include "message_bus/bus.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

#include <zmq.h>
#include <zmq.hpp>

namespace mbus
{

    Bus::Bus(const std::shared_ptr<zmq::context_t> &ctx)
        : ctx_(ctx)
    {
        backend_ =
            std::make_unique<zmq::socket_t>(*ctx_, zmq::socket_type::xpub);
        assert(backend_ && "Out Of Memory!!");

        frontend_ =
            std::make_unique<zmq::socket_t>(*ctx_, zmq::socket_type::xsub);
        assert(frontend_ && "Out Of Memory!!");

        log_ = std::make_unique<zmq::socket_t>(*ctx_, zmq::socket_type::pair);
        // log_ = zmq_socket(*ctx, zmq::socket_type::pair);
        assert(log_ && "Out Of Memory!!");
    }

    Bus::~Bus()
    {
        if (worker_.joinable())
        {
            worker_.join();
        }
    }

    void Bus::Run(std::string host, int32_t xpub_port, int32_t xsub_port)
    {
        Bind(host, xpub_port, xsub_port);
        std::this_thread::sleep_for(std::chrono::seconds{1});
        worker_ = std::thread{[&] {
            spdlog::info("Running...");
            /// FIXME: 临时移除异步日志 socket
            zmq::proxy(zmq::socket_ref{*frontend_}, *backend_);
            // zmq::proxy(*frontend_, *backend_);
            spdlog::info("Stopped...");
        }};
    }

    /// 尝试启动仅支持本机进程间通信的服务
    bool Bus::TryRunOnLocalMode(int32_t xpub_port, int32_t xsub_port)
    {
        auto result = TryBindOnLocalMode(xpub_port, xsub_port);
        if (result)
        {
            worker_ = std::thread{[&] {
                zmq::proxy(*frontend_, *backend_);
            }};
        }
        return result;
    }

    void Bus::Bind(std::string host, int32_t xpub_port, int32_t xsub_port)
    {
        assert(ctx_);
        assert(!host.empty());
        assert(xpub_port > 1023 && xpub_port < 65535);
        assert(xsub_port > 1023 && xsub_port < 65535);

        // std::cout << "正在启动服务..." << std::endl;
        spdlog::info("starting service...");

        auto pub_addr = std::string{host};
        pub_addr += ":";
        pub_addr += std::to_string(xpub_port);
        // std::cout << "发布端需要连接地址: " << pub_addr << std::endl;
        spdlog::info("The address that the publisher needs to connect to is {}", pub_addr);
        backend_->bind(pub_addr);

        auto sub_addr = std::string{host};
        sub_addr += ":";
        sub_addr += std::to_string(xsub_port);
        // std::cout << "订阅端需要连接地址: " << sub_addr << std::endl;
        spdlog::info("The address that the subscriber needs to connect to is {}", sub_addr);
        frontend_->bind(sub_addr);

        log_->bind("ipc://log");
    }

    bool Bus::TryBindOnLocalMode(int32_t xpub_port, int32_t xsub_port)
    {
        /// NOTE: 不考虑端口被非本系统节点程序占用的问题，启动前用户自行处理预期外的端口占用
        /// 情况。也不考虑大量程序同时启动的问题，用户自行保证程序按一定时间间隔启动。
        /// 启动策略：按 xpub -> xsub 的顺序绑定端口，
        /// 如果绑定失败了，随机一段时间后重试，最多重试 3 次，还是失败返回 false。

        auto pub_addr = std::string{"tcp://*:"};
        pub_addr += std::to_string(xpub_port);
        auto sub_addr = std::string{"tcp://*:"};
        sub_addr += std::to_string(xsub_port);

        bool pub_bind_success = true;
        bool sub_bind_success = true;

        for (size_t i = 0; i < 3; i++)
        {
            auto random_sleep = std::chrono::milliseconds(rand() % 333);
            std::this_thread::sleep_for(random_sleep);

            try
            {
                backend_->bind(pub_addr);
                // std::cout << "订阅端需要连接地址: " << pub_addr << std::endl;
                spdlog::info("The address that the subscriber needs to connect to is {} (pub_addr)", pub_addr);
            }
            catch (std::exception &ex)
            {
                std::cerr << "尝试启动消息总线服务" << std::endl;
                pub_bind_success = false;
                continue;
            }
            pub_bind_success = true;
            break;
        }

        if (!pub_bind_success)
        {
            return false;
        }

        for (size_t i = 0; i < 3; i++)
        {
            try
            {
                frontend_->bind(sub_addr);
                // std::cout << "发布端需要连接地址: " << pub_addr << std::endl;
                spdlog::info("The address that the publisher needs to connect to is {}", pub_addr);
            }
            catch (std::exception &ex)
            {
                std::cerr << ex.what() << std::endl;
                sub_bind_success = false;
                auto random_sleep = std::chrono::milliseconds(rand() % 333);
                std::this_thread::sleep_for(random_sleep);
                continue;
            }
            sub_bind_success = true;
            break;
        }

        return sub_bind_success;
    }

} // namespace mbus
