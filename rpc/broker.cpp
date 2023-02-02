#include "rpc/broker.h"
#include "common/constants.h"
#include "common/time.h"
#include "common/uuid.h"
#include "common/zmq_helper.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>

#include <spdlog/spdlog.h>
#include <string>
#include <thread>
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

    /// 移除该 worker 要下线的 service 名称
    void Worker::DelServiceName(const std::string &name)
    {
        auto it = service_names_.find(name);
        if (it != service_names_.end())
        {
            service_names_.erase(it);
        }
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
            if (worker.expired() || worker.lock()->Id() == worker_id)
            {
                spdlog::warn("remove worker_id is {}", worker_id);
            }

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
        spdlog::info("name is {} and requests.length is {} and worker_.length is {}", name_, requests_.size(), worker_.size());

        /// 处理worker不存在的情况
        while (!requests_.empty())
        {
            broker_->AddUnHandleRequest(std::move(requests_.front()));
            requests_.pop_front();
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
        // std::cout << "rpc 代理服务地址: " << endpoint << std::endl;
        spdlog::info("rpc agent service address is {}", endpoint);
    }

    void RpcBroker::HandleRequest()
    {
        while (!stop_)
        {
            if (messages_task_.empty())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            auto &messages = messages_task_.front();
            if (debug_)
            {
                // std::cout << "收到到消息：" << StringifyMessages(messages) << std::endl;
                spdlog::info("Received the message: {}", StringifyMessages(messages));
            }
            auto routing_id = std::move(messages.front());
            messages.pop_front();
            assert(routing_id.size() == 16);

            auto serial_num = std::move(messages.front());
            messages.pop_front();

            // 请求来自 worker
            if (Equal(messages.front(), RPC_WORKER))
            {
                messages.pop_front();
                HandleWorkerMessage(std::move(routing_id), std::move(serial_num), std::move(messages));
            }
            // 请求来自 client
            else if (Equal(messages.front(), RPC_CLIENT))
            {
                messages.pop_front();
                HandleClientMessage(std::move(routing_id), std::move(serial_num), std::move(messages));
            }
            else
            {
                assert(false && "非法请求");
            }
            messages_task_.pop_front();
        }
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
                spdlog::info("Prepare to receive data");
                auto messages = ReceiveAll(*socket_);
                if (debug_)
                {
                    // std::cout << "收到到消息：" << StringifyMessages(messages) << std::endl;
                    spdlog::info("Received the message: {}", StringifyMessages(messages));
                }
                // messages_task_.push_back(std::move(messages));

                auto routing_id = std::move(messages.front());
                messages.pop_front();
                assert(routing_id.size() == 16);

                auto serial_num = std::move(messages.front());
                messages.pop_front();

                // 请求来自 worker
                if (Equal(messages.front(), RPC_WORKER))
                {
                    messages.pop_front();
                    HandleWorkerMessage(std::move(routing_id), std::move(serial_num), std::move(messages));
                }
                // 请求来自 client
                else if (Equal(messages.front(), RPC_CLIENT))
                {
                    messages.pop_front();
                    HandleClientMessage(std::move(routing_id), std::move(serial_num), std::move(messages));
                }
                else
                {
                    assert(false && "非法请求");
                }
            }
            else
            {
                // 超时时删除过期的 worker
                // fixme 当不断接收到消息时，此分支时不会被执行到的
                auto current_time = Now() - HEARTBEAT_EXPIRY;
                auto it = workers_.begin();
                while (it != workers_.end())
                {
                    auto cur_worker = it->second;
                    spdlog::info("worker id is {} and expirt is {} and current_time is {}", cur_worker->Id(), cur_worker->GetExpiry(), current_time);
                    spdlog::info("expiry of cur_worker is {}", cur_worker->GetExpiry());
                    if (cur_worker->GetExpiry() < current_time)
                    {
                        spdlog::warn("the worker id is {} will be removed on else", cur_worker->Id());
                        auto service_it = services_.begin();
                        while (service_it != services_.end())
                        {
                            spdlog::warn("the service {} will be removed on else", service_it->first);
                            service_it->second->RemoveWorker(cur_worker->Id());
                            service_it++;
                        }
                        it->second = nullptr;
                        workers_.erase(it++);
                    }
                    else
                    {
                        ++it;
                    }
                }
                spdlog::info("out delete timeout service");
            }
            DeleteTimeoutWorker(workers_, services_);
            // 分发各个 service 中的请求到 worker 去
            Dispacth();
        }
    }

    void RpcBroker::DeleteTimeoutWorker(std::unordered_map<std::string, std::shared_ptr<Worker>> &workers, std::unordered_map<std::string, std::unique_ptr<Service>> &services)
    {
        // 超时时删除过期的 worker
        auto current_time = Now() - HEARTBEAT_EXPIRY;
        auto it = workers_.begin();
        while (it != workers_.end())
        {
            auto cur_worker = it->second;
            spdlog::info("worker id is {} and expirt is {} and current_time is {}", cur_worker->Id(), cur_worker->GetExpiry(), current_time);
            spdlog::info("expiry of cur_worker is {}", cur_worker->GetExpiry());
            if (cur_worker->GetExpiry() < current_time)
            {
                spdlog::warn("the worker id is {} will be removed", cur_worker->Id());
                auto service_it = services_.begin();
                while (service_it != services_.end())
                {
                    spdlog::warn("the service {} will be removed", service_it->first);
                    service_it->second->RemoveWorker(cur_worker->Id());
                    service_it++;
                }
                it->second = nullptr;
                workers_.erase(it++);
            }
            else
            {
                ++it;
            }
        }
    }

    void RpcBroker::SendMessage(const std::string &routing_id, MessagePack &messages)
    {
        messages.push_front(MakeMessage(routing_id));
        if (debug_)
        {
            // std::cout << "分发请求 " << StringifyMessages(messages) << std::endl;
            spdlog::info("distribution request is {}", StringifyMessages(messages));
        }
        SendAll(*socket_, messages, send_mu_);
    }

    /// 获取或创建新的 worker 记录
    std::weak_ptr<Worker> RpcBroker::RequireWorker(const std::string &worker_id)
    {
        auto iterator = workers_.find(worker_id);
        if (iterator == workers_.end())
        {
            if (debug_)
            {
                // std::cout << "创建新的 worker 记录，worker id: " << UUID2String(worker_id) << std::endl;
                spdlog::info("create a new worker record, worker id is {}", UUID2String(worker_id));
            }

            workers_[worker_id] = std::make_shared<Worker>(this, worker_id);
            workers_[worker_id]->SetExpiry(Now() + HEARTBEAT_INTERVAL);
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
                // std::cout << "创建新的 service 记录，service name: " << service_name << std::endl;
                spdlog::info("create a new service record, service name is {}", service_name);
            }

            services_[service_name] = std::make_unique<Service>(this, service_name);
        }

        iterator = services_.find(service_name);
        auto service = iterator->second.get();
        assert(service != nullptr);
        return service;
    }

    /// 下线 service 记录
    void RpcBroker::DeleteService(const std::string &service_name)
    {
        auto iterator = services_.find(service_name);
        if (iterator == services_.end())
        {
            if (debug_)
            {
                // std::cout << "删除 service 记录时没有该服务记录，service name: " << service_name << std::endl;
                spdlog::info("There is no such service record when deleting the service record, service name is {}", service_name);
            }
            return;
        }
        if (debug_)
        {
            // std::cout << "删除新的 service 记录，service name: " << service_name << std::endl;
            spdlog::info("delete service record, service name is {}", service_name);
        }
        iterator->second->RemoveWorker("");
        iterator->second = nullptr;
        services_.erase(iterator);
    }

    /// @brief 处理 worker 的请求，服务注册、心跳包、断开链接
    /// @param worker_id 发送该请求的 worker 的 uuid
    /// @param messages 请求携带的全部 message
    void RpcBroker::HandleWorkerMessage(zmq::message_t worker_id, zmq::message_t serial_num, MessagePack messages)
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
                    // std::cout << UUID2String(id_string) << " 注册服务 " << service_name << std::endl;
                    spdlog::info("worker id is {} to sign in service, service name is {}", UUID2String(id_string), service_name);
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
            spdlog::info("respond to client and response uuid is {} and serial_num is {}", UUID2String(routing_id.to_string()), serial_num.to_string());
            // messages 目前只剩下服务名和响应内容，需要将相关的头部信息拼接回去，发给客户端
            messages.push_front(MakeMessage(RPC_RES));
            messages.push_front(MakeMessage(RPC_WORKER));
            messages.push_front(std::move(serial_num));
            messages.push_front(std::move(routing_id));
            SendAll(*socket_, messages, send_mu_);
        }
        // 收到 worker 的心跳包
        else if (Equal(header, RPC_HB))
        {
            // 设置当前 worker 的超时时间，在这个时间之后没收到新的心跳包就要删除
            spdlog::info("the id of recevied the beat of worker is {}", worker->Id());
            worker->SetExpiry(Now() + HEARTBEAT_INTERVAL);
        }
        // 处理 worker 的下线请求
        else if (Equal(header, RPC_UNCONNECT))
        {
            for (const auto &name : messages)
            {
                assert(!name.empty());
                std::string service_name{name.data<char>(), name.size()};
                DeleteService(service_name);
                auto cur_service = RequireService(service_name);
                cur_service->RemoveWorker(id_string);
                worker->DelServiceName(service_name);
                if (debug_)
                {
                    // std::cout << UUID2String(id_string) << " 下线服务 " << service_name << std::endl;
                    spdlog::info("worker id is {} to off line service, service name is {}", UUID2String(id_string), service_name);
                }
            }
            if (worker->GetSizeOfService() == 0)
            {
                worker->SetExpiry(Now());
            }
            ResponseSuccess(id_string);
        }

        // todo 下线处理
    }

    /// 处理 client 的请求，服务查询、服务请求分发
    /// @param client_id 发送该请求的 client 的 uuid
    /// @param messages 请求携带的全部 message
    void RpcBroker::HandleClientMessage(zmq::message_t client_id, zmq::message_t serial_num, MessagePack messages)
    {
        assert(client_id.size() == 16);
        assert(messages.size() >= 1);

        auto id_string = client_id.to_string();

        if (Equal(messages.front(), RPC_REQ))
        {
            // 取出 service 部分
            auto service_name = messages[1].to_string();
            auto *service = RequireService(service_name);

            // 将请求加入到对应服务的请求队列中
            if (debug_)
            {
                // std::cout << "来自 " << UUID2String(id_string) << " 的请求加入 " << service_name << std::endl
                //           << "请求内容 " << StringifyMessages(messages) << std::endl;
                spdlog::info("the request that come from worker id {} and serial_num is {} to join service {} and content of request is {}", UUID2String(id_string), serial_num.to_string(), service_name, StringifyMessages(messages));
            }

            // 重新封包被弹出的协议头
            messages.push_front(MakeMessage(RPC_CLIENT));
            messages.push_front(std::move(serial_num));
            messages.push_front(std::move(client_id));

            service->AddRequest(std::move(messages));
        }
    }

    /// 分发请求到对应的 worker
    void RpcBroker::Dispacth()
    {
        for (auto &service : services_)
        {
            spdlog::info("service is {} to dispacth", service.first);
            service.second->Dispacth();
        }
        while (!not_processed_.empty())
        {
            auto messages = std::move(not_processed_.front());
            not_processed_.pop_front();
            auto worker_id = std::move(messages.front());
            messages.pop_front();
            auto serial_id = std::move(messages.front());
            messages.pop_front();
            assert(worker_id.size() == 16);
            ResponseNotProcessed(worker_id.to_string(), serial_id.to_string());
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

        SendAll(*socket_, messages, send_mu_);
    }

    /// 回复给client 请求未处理的失败
    void RpcBroker::ResponseNotProcessed(const std::string &client_id, const std::string &serial_id)
    {
        BufferPack messages;
        messages.push_back(MakeZmqBuffer(client_id));
        messages.push_back(MakeZmqBuffer(serial_id));
        messages.push_back(MakeZmqBuffer(RPC_BROKER));
        messages.push_back(MakeZmqBuffer(RPC_NOTPROCESSED));

        SendAll(*socket_, messages, send_mu_);
    }

} // namespace mbus