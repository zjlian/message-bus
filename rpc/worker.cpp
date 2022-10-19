#include "rpc/worker.h"
#include "common/constants.h"
#include "common/time.h"
#include "common/uuid.h"
#include "common/zmq_helper.h"

#include <cassert>
#include <cstring>
#include <mutex>
#include <utility>
#include <vector>

#include <zmq.hpp>

namespace mbus
{

    RpcWorker::RpcWorker(const std::shared_ptr<zmq::context_t> &ctx)
        : ctx_(ctx), uuid_(UUID())
    {
        assert(ctx_ != nullptr);
    }

    RpcWorker::~RpcWorker()
    {
        stop_ = true;
        if (worker_.joinable())
        {
            worker_.join();
        }
    }

    void RpcWorker::Debug()
    {
        debug_ = true;
    }

    /// 链接代理服务，多次调用会断开连接后重连
    void RpcWorker::Connect(const std::string &broker_addr)
    {
        assert(ctx_ != nullptr);
        assert(!broker_addr.empty());

        if (debug_ && socket_ == nullptr)
        {
            auto str = UUID2String(uuid_);
            std::cout << "当前进程的 rpc worker uuid: " << str << std::endl;
        }

        socket_ = std::make_unique<zmq::socket_t>(*ctx_, zmq::socket_type::dealer);
        assert(socket_ != nullptr && "Out Of Memory!!!");
        // 设置 socket 的标识符
        socket_->set(zmq::sockopt::routing_id, uuid_);
        // 设置 socket 关闭时丢弃所有未发送成功的消息
        // socket_->set(zmq::sockopt::linger, 0);

        broker_addr_ = broker_addr;
        socket_->connect(broker_addr_);
        if (debug_)
        {
            std::cout << "连接 rpc 代理服务: " << broker_addr_ << std::endl;
        }

        // 发送话题注册请求
        BufferPack pack;
        pack.push_back(MakeZmqBuffer(RPC_WORKER));
        pack.push_back(MakeZmqBuffer(RPC_REG));
        std::unique_lock<std::mutex> lock{services_mutex_};
        for (const auto &item : services_)
        {
            pack.push_back(MakeZmqBuffer(item.first));
        }
        lock.unlock();

        if (debug_)
        {
            std::cout << "发送服务注册请求: " << StringifyMessages(pack) << std::endl;
        }
        SendAll(*socket_, pack);
        if (debug_)
        {
            std::cout << "等待 broker 响应" << std::endl;
        }
        auto res = ReceiveAll(*socket_);

        if (debug_)
        {
            std::cout << "broker 响应 " << StringifyMessages(res) << std::endl;
        }

        worker_ = std::thread{[&] {
            std::cout << "worker io 线程启动" << std::endl;
            stop_ = false;
            ReceiveLoop();
        }};
    }

    /// 重新连接上一次连接的代理服务
    void RpcWorker::Reconnect()
    {
        assert(ctx_ != nullptr);
        assert(!broker_addr_.empty());

        Connect(broker_addr_);
    }

    /// 向代理端注册想要接收的服务请求
    /// @thread-safe
    void RpcWorker::RegisterService(const std::string &service_name, const std::function<ServiceCallback> &callback)
    {
        std::lock_guard<std::mutex> lock{services_mutex_};
        services_[service_name] = callback;
    }

    /// 设置信号检测延迟
    void RpcWorker::SetHeartbeat(int32_t heartbeat)
    {
        heartbeat_ = heartbeat;
    }

    /// 设置重连延迟
    void RpcWorker::SetReconnectDelay(int32_t delay)
    {
        reconnect_delay_ = delay;
    }

    /// 响应请求，client_uuid 为目标客户端的标识符，body 为响应的内容
    void RpcWorker::Response(const std::string &client_uuid, const std::string &service_name, const std::string &body)
    {
        assert(ctx_ != nullptr);
        assert(socket_ != nullptr);
        assert(!broker_addr_.empty());

        std::vector<zmq::const_buffer> messages;
        messages.reserve(5);
        messages.push_back(MakeZmqBuffer(RPC_WORKER));
        messages.push_back(MakeZmqBuffer(RPC_RES));
        messages.push_back(MakeZmqBuffer(client_uuid));
        messages.push_back(MakeZmqBuffer(service_name));
        messages.push_back(MakeZmqBuffer(body));

        if (debug_)
        {
            std::cout << "响应 RPC 请求，回复内容" << StringifyMessages(messages) << std::endl;
        }

        SendAll(*socket_, messages);
        // TODO 错误处理
    }

    /// 发送消息到代理端
    void RpcWorker::SendToBroker(const std::string &header, const std::string &command, const std::string &body)
    {
        std::vector<zmq::const_buffer> messages;
        messages.reserve(4);
        messages.push_back(MakeZmqBuffer(RPC_WORKER));
        messages.push_back(MakeZmqBuffer(header));
        if (!command.empty())
        {
            messages.push_back(MakeZmqBuffer(command));
        }
        if (!body.empty())
        {
            messages.push_back(MakeZmqBuffer(body));
        }

        if (debug_)
        {
            std::cout << "发送消息到代理端 " << StringifyMessages(messages) << std::endl;
        }

        SendAll(*socket_, messages);
        // TODO 错误处理
    }

    void RpcWorker::ReceiveLoop()
    {
        assert(ctx_ != nullptr);
        assert(socket_ != nullptr);
        assert(!broker_addr_.empty());
        assert(stop_ == false);

        while (!stop_)
        {
            if (WaitReadable(*socket_, heartbeat_))
            {
                if (debug_)
                {
                    std::cout << "等待 rpc 请求..." << std::endl;
                }

                auto messages = ReceiveAll(*socket_);
                assert(messages.size() >= 4);

                if (debug_)
                {
                    std::cout << "收到客户端的 rpc 请求：" << StringifyMessages(messages) << std::endl;
                }

                auto req_uuid = std::move(messages.front());
                messages.pop_front();
                assert(req_uuid.size() == 16 && "客户端 uuid 错误");

                auto req_actor = std::move(messages.front());
                messages.pop_front();
                assert(Equal(req_actor, RPC_CLIENT) && "客户端协议头错误");

                auto req_header = std::move(messages.front());
                messages.pop_front();
                assert(Equal(req_header, RPC_REQ) && "客户端协议头错误");

                auto req_service = std::move(messages.front());
                messages.pop_front();
                assert(req_service.size() != 0 && "客户端请求的服务名不能为空");
                auto req_argument = std::move(messages.front());
                messages.pop_front();

                auto service_name = req_service.to_string();
                auto argumnet = req_argument.to_string();
                std::lock_guard<std::mutex> lock{services_mutex_};
                auto iterator = services_.find(service_name);
                assert(iterator != services_.end() && "服务未注册");

                auto result = iterator->second(argumnet);
                Response(req_uuid.to_string(), service_name, result);
            }
            else
            {
                // TODO 长时间没有收到请求，断开重连代理服务

                // 定时发送心跳包到代理端
                if (Now() >= heartbeat_at_)
                {
                    SendToBroker(RPC_HB, "", "");
                    heartbeat_at_ = Now() + heartbeat_;
                }
            }
        }
    }

} // namespace mbus