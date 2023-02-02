#pragma once

#include "common/macro_utility.h"
#include "common/zmq_helper.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <unordered_set>
#include <vector>
#include <zmq.hpp>

namespace mbus
{
    class RpcBroker;
    class Worker;
    class Service;

    /// service 信息登记
    class Service
    {
    public:
        DISABLE_COPY(Service);

        Service(RpcBroker *broker, const std::string &name);

        /// 比较是否为同一个 worker
        bool operator==(const Service &other);

        /// 添加新请求
        void AddRequest(MessagePack &&pack);

        /// 添加新的 worker
        void AddWorker(std::weak_ptr<Worker> worker);

        /// 移除 worker
        void RemoveWorker(const std::string &worker_id);

        /// 分发请求队列的消息到 worker 里
        void Dispacth();

    private:
        // 保存了本 service 的 broker
        RpcBroker *broker_{nullptr};
        // service 名称
        std::string name_{};
        // 来自 client 的全部 rpc 请求
        std::deque<MessagePack> requests_{};
        // 能够处理该 service 请求的 worker
        std::deque<std::weak_ptr<Worker>> worker_{};
    };

    class Worker
    {
    public:
        DISABLE_COPY(Worker);

        Worker(RpcBroker *broker, const std::string &id);

        /// 比较是否为同一个 worker
        bool operator==(const Worker &other);

        /// 获取 worker 的 id
        const std::string &Id();

        /// 添加该 worker 能处理 service 名称
        void AddServiceName(const std::string &name);

        /// 移除该 worker 请求下线的 service 名称
        void DelServiceName(const std::string &name);

        /// 移除该worker 所有的service
        void DelAllServiceName();

        /// 获取该 worker 所提供的服务名称
        int GetSizeOfService() const
        {
            return service_names_.size();
        }

        /// 设置心跳包超时时间
        void SetExpiry(int64_t expiry);

        /// 获取超时时间
        int64_t GetExpiry();

    private:
        // 保存了本 worker 的 broker
        RpcBroker *broker_{nullptr};
        // worker 的唯一 id
        std::string identity_{};
        // worker 提供的全部服务的名称
        std::unordered_set<std::string> service_names_{};
        // 到期时间，时间段内没收到对应的 worker 程序的心跳包，就删除该 worker
        int64_t expiry_{};
    };

    class RpcBroker
    {
    public:
        DISABLE_COPY_AND_MOVE(RpcBroker);

        RpcBroker(const std::shared_ptr<zmq::context_t> &ctx);
        ~RpcBroker() = default;

        void Debug();

        /// 在指定的 zmq addr 上开启代理服务
        void Bind(const std::string &endpoint);

        // 启动代理服务
        void Run();

        void HandleRequest();

        /// 添加无法处理的请求，由broker处理响应
        void AddUnHandleRequest(MessagePack &&pack)
        {
            not_processed_.emplace_back(std::move(pack));
        }

        // 发送消息到指定 routing_id 的客户端，可能是 worker 也可能是 client
        void SendMessage(const std::string &routing_id, MessagePack &messages);

    private:
        /// 获取或创建新的 worker 记录
        std::weak_ptr<Worker> RequireWorker(const std::string &worker_id);

        /// 获取或创建新的 service 记录
        Service *RequireService(const std::string &service_name);

        /// 删除 service 记录
        void DeleteService(const std::string &service_name);

        /// 删除 超时 worker
        void DeleteTimeoutWorker(std::unordered_map<std::string, std::shared_ptr<Worker>> &workers, std::unordered_map<std::string, std::unique_ptr<Service>> &services);

        /// @brief 处理 worker 的请求，服务注册、心跳包、断开链接
        /// @param worker_id 发送该请求的 worker 的 uuid
        /// @param messages 请求携带的全部 message
        void HandleWorkerMessage(zmq::message_t worker_id, zmq::message_t serial_num, MessagePack messages);

        /// 处理 client 的请求，服务查询、服务请求分发
        /// @param client_id 发送该请求的 client 的 uuid
        /// @param messages 请求携带的全部 message
        void HandleClientMessage(zmq::message_t client_id, zmq::message_t serial_num, MessagePack messages);

        /// 删除心跳包超时的 worker
        void Purge();

        /// 分发请求到对应的 worker
        void Dispacth();

        /// 回复 worker 请求成功
        void ResponseSuccess(const std::string &worker_id);

        /// 回复 client 请求未处理的响应
        void ResponseNotProcessed(const std::string &client_id, const std::string &);

    private:
        std::atomic<bool> debug_{false};
        std::atomic<bool> stop_{true};
        std::shared_ptr<zmq::context_t> ctx_{};
        std::unique_ptr<zmq::socket_t> socket_{};
        std::string bind_addr{};

        // 收到到所有消息的队列
        std::deque<std::deque<zmq::message_t>> messages_task_{};
        std::unordered_map<std::string, std::unique_ptr<Service>> services_{};
        std::unordered_map<std::string, std::shared_ptr<Worker>> workers_{};
        std::unordered_map<std::string, int> waiting_worker_{};
        // 无法处理的 client 的rpc请求
        std::deque<MessagePack> not_processed_{};
        uint64_t heartbeat_at_{};
        std::mutex send_mu_;
    };

} // namespace mbus