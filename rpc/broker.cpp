#include "rpc/broker.h"
#include "common/constants.h"
#include "common/time.h"
#include "common/uuid.h"
#include "common/zmq_helper.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <memory>

#include <string>
#include <utility>
#include <zmq.hpp>

namespace mbus
{
    /*------ Worker 实现 ------*/
    Worker::Worker(RpcBroker *broker, const std::string &id)
        : broker_(broker), identity_(id)
    {
    }

    /// 比较是否为同一个 worker
    bool Worker::operator==(const Worker &other)
    {
        return identity_ == other.identity_;
    }

    /// 获取 worker 的 id
    const std::string &Worker::Id()
    {
        return identity_;
    }

    /// 添加该 worker 能处理 service 名称
    void Worker::AddServiceName(const std::string &name)
    {
        service_names_.insert(name);
    }

    /// 设置心跳包超时时间
    void Worker::SetExpiry(int64_t expiry)
    {
        expiry_ = expiry;
    }

    /// 获取超时时间
    int64_t Worker::GetExpiry()
    {
        return expiry_;
    }

    /*------ Service 实现 ------*/
    Service::Service(RpcBroker *broker, const std::string &name)
        : broker_(broker), name_(name)
    {
    }

    /// 比较是否为同一个 worker
    bool Service::operator==(const Service &other)
    {
        return name_ == other.name_;
    }

    /// 添加新请求
    void Service::AddRequest(MessagePack &&pack)
    {
        requests_.emplace_back(std::move(pack));
    }

    /// 添加新的 worker
    void Service::AddWorker(std::weak_ptr<Worker> worker)
    {
        worker_.emplace_back(std::move(worker));
    }

    /// 移除 worker
    void Service::RemoveWorker(const std::string &worker_id)
    {
        auto remove = std::remove_if(worker_.begin(), worker_.end(), [&](std::weak_ptr<Worker> worker) {
            // 移除 weak_ptr 失效和指定要删除的
            return worker.expired() || worker.lock()->Id() == worker_id;
        });

        worker_.erase(remove, worker_.end());
    }

    /// 分发请求队列的消息到 worker 里
    void Service::Dispacth()
    {
        // 如果存在 weak_ptr 失效的 worker，先移除
        RemoveWorker("");

        while (!requests_.empty() && !worker_.empty())
        {
            // TODO 后续如果有多个 worker 需要将请求均发到多个 worker 上。
            // 目前先实现只发给第一个 worker
            auto messages = std::move(requests_.front());
            requests_.pop_front();

            auto worker = worker_.front().lock();
            broker_->SendMessage(worker->Id(), messages);
        }
    }

    /*------ RpcBroken 实现 ------*/
    RpcBroker::RpcBroker(const std::shared_ptr<zmq::context_t> &ctx)
        : ctx_(ctx)
    {
        assert(ctx_ != nullptr);

        socket_ = std::make_unique<zmq::socket_t>(*ctx_, zmq::socket_type::router);
        assert(socket_ != nullptr && "Out Of Memory!!!");
        heartbeat_at_ = Now() + HEARTBEAT_INTERVAL;
    }

    void RpcBroker::Debug()
    {
        debug_ = true;
    }

    void RpcBroker::Bind(const std::string &endpoint)
    {
        assert(ctx_ != nullptr);
        assert(socket_ != nullptr);
        assert(!endpoint.empty());

        socket_->bind(endpoint);
        std::cout << "rpc 代理服务地址: " << endpoint << std::endl;
    }

    void RpcBroker::Run()
    {
        assert(stop_ == true);
        assert(ctx_ != nullptr);
        assert(socket_ != nullptr);

        stop_ = false;
        while (!stop_)
        {
            if (WaitReadable(*socket_, HEARTBEAT_INTERVAL))
            {
                auto messages = ReceiveAll(*socket_);
                if (debug_)
                {
                    std::cout << "收到到消息：" << StringifyMessages(messages) << std::endl;
                }

                auto routing_id = std::move(messages.front());
                messages.pop_front();
                assert(routing_id.size() == 16);

                // 请求来自 worker
                if (Equal(messages.front(), RPC_WORKER))
                {
                    messages.pop_front();
                    HandleWorkerMessage(std::move(routing_id), std::move(messages));
                }
                // 请求来自 client
                else if (Equal(messages.front(), RPC_CLIENT))
                {
                    messages.pop_front();
                    HandleClientMessage(std::move(routing_id), std::move(messages));
                }
                else
                {
                    assert(false && "非法请求");
                }
            }
            else
            {
                // 超时时删除过期的 worker
            }

            // 分发各个 service 中的请求到 worker 去
            Dispacth();
        }
    }

    void RpcBroker::SendMessage(const std::string &routing_id, MessagePack &messages)
    {
        messages.push_front(MakeMessage(routing_id));
        if (debug_)
        {
            std::cout << "分发请求 " << StringifyMessages(messages) << std::endl;
        }
        SendAll(*socket_, messages);
    }

    /// 获取或创建新的 worker 记录
    std::weak_ptr<Worker> RpcBroker::RequireWorker(const std::string &worker_id)
    {
        auto iterator = workers_.find(worker_id);
        if (iterator == workers_.end())
        {
            if (debug_)
            {
                std::cout << "创建新的 worker 记录，worker id: " << UUID2String(worker_id) << std::endl;
            }

            workers_[worker_id] = std::make_shared<Worker>(this, worker_id);
        }

        iterator = workers_.find(worker_id);
        auto worker = iterator->second;
        assert(worker->Id() == worker_id);

        return worker;
    }

    /// 获取或创建新的 service 记录
    Service *RpcBroker::RequireService(const std::string &service_name)
    {
        auto iterator = services_.find(service_name);
        if (iterator == services_.end())
        {
            if (debug_)
            {
                std::cout << "创建新的 service 记录，service name: " << service_name << std::endl;
            }

            services_[service_name] = std::make_unique<Service>(this, service_name);
        }

        iterator = services_.find(service_name);
        auto service = iterator->second.get();
        assert(service != nullptr);
        return service;
    }

    /// @brief 处理 worker 的请求，服务注册、心跳包、断开链接
    /// @param worker_id 发送该请求的 worker 的 uuid
    /// @param messages 请求携带的全部 message
    void RpcBroker::HandleWorkerMessage(zmq::message_t worker_id, MessagePack messages)
    {
        assert(worker_id.size() == 16);
        assert(messages.size() >= 1);

        auto id_string = worker_id.to_string();
        bool new_worker = (workers_.find(id_string) == workers_.end());
        auto worker_wp = RequireWorker(id_string);
        auto worker = worker_wp.lock();

        auto header = std::move(messages.front());
        messages.pop_front();
        assert(header.size() >= 6);

        // 收到 worker 的服务注册请求
        if (Equal(header, RPC_REG))
        {
            for (const auto &name : messages)
            {
                assert(!name.empty());
                std::string service_name{name.data<char>(), name.size()};
                auto *service = RequireService(service_name);
                worker->AddServiceName(service_name);
                service->AddWorker(worker_wp);

                if (debug_)
                {
                    std::cout << UUID2String(id_string) << " 注册服务 " << service_name << std::endl;
                }
            }
            ResponseSuccess(id_string);
        }
        // 收到 worker 回复的 rpc 请求结果
        else if (Equal(header, RPC_RES))
        {
            auto routing_id = std::move(messages.front());
            messages.pop_front();
            assert(routing_id.size() == 16 && "rpc 响应转发的客户端 id 错误");

            // messages 目前只剩下服务名和响应内容，需要将相关的头部信息拼接回去，发给客户端
            messages.push_front(MakeMessage(RPC_RES));
            messages.push_front(MakeMessage(RPC_WORKER));
            messages.push_front(std::move(routing_id));

            SendAll(*socket_, messages);
        }
        // 收到 worker 的心跳包
        else if (Equal(header, RPC_HB))
        {
            // 设置当前 worker 的超时时间，在这个时间之后没收到新的心跳包就要删除
            worker->SetExpiry(Now() + HEARTBEAT_INTERVAL);
        }
    }

    /// 处理 client 的请求，服务查询、服务请求分发
    /// @param client_id 发送该请求的 client 的 uuid
    /// @param messages 请求携带的全部 message
    void RpcBroker::HandleClientMessage(zmq::message_t client_id, MessagePack messages)
    {
        assert(client_id.size() == 16);
        assert(messages.size() >= 1);

        auto id_string = client_id.to_string();

        if (Equal(messages.front(), RPC_REQ))
        {
            // 取出 service 部分
            auto service_name = messages[1].to_string();
            auto *service = RequireService(service_name);

            // 重新封包被弹出的协议头
            messages.push_front(MakeMessage(RPC_CLIENT));
            messages.push_front(std::move(client_id));

            // 将请求加入到对应服务的请求队列中
            if (debug_)
            {
                std::cout << "来自 " << UUID2String(id_string) << " 的请求加入 " << service_name << std::endl
                          << "请求内容 " << StringifyMessages(messages) << std::endl;
            }
            service->AddRequest(std::move(messages));
        }
    }

    /// 分发请求到对应的 worker
    void RpcBroker::Dispacth()
    {
        for (auto &service : services_)
        {
            service.second->Dispacth();
        }
    }

    /// 回复 worker 请求成功
    void RpcBroker::ResponseSuccess(const std::string &worker_id)
    {
        auto iterator = workers_.find(worker_id);
        assert(iterator != workers_.end());

        BufferPack messages;
        messages.push_back(MakeZmqBuffer(worker_id));
        messages.push_back(MakeZmqBuffer(RPC_SUCCESS));

        SendAll(*socket_, messages);
    }

} // namespace mbus