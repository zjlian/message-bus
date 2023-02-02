#include "rpc/worker.h"
#include "common/constants.h"
#include "common/time.h"
#include "common/uuid.h"
#include "common/zmq_helper.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <spdlog/spdlog.h>
#include <string>
#include <thread>
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
        if (handler_.joinable())
        {
            handler_.join();
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
            // std::cout << "当前进程的 rpc worker uuid: " << str << std::endl;
            spdlog::info("uuid of rpc worker of current process is {}", str);
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
            // std::cout << "连接 rpc 代理服务: " << broker_addr_ << std::endl;
            spdlog::info("connect rpc proxy service: {}", broker_addr_);
        }

        // 发送话题注册请求
        BufferPack pack;
        auto serial_num = std::to_string(Now());
        pack.push_back(MakeZmqBuffer(serial_num));
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
            // std::cout << "发送服务注册请求: " << StringifyMessages(pack) << std::endl;
            spdlog::info("Send service registration request: {}", StringifyMessages(pack));
        }
        SendAll(*socket_, pack, send_mx_);
        if (debug_)
        {
            // std::cout << "等待 broker 响应" << std::endl;
            spdlog::info("Wait for broker response");
        }
        auto res = ReceiveAll(*socket_);

        if (debug_)
        {
            // std::cout << "broker 响应 " << StringifyMessages(res) << std::endl;
            spdlog::info("message of response of broker is {}", StringifyMessages(res));
        }

        worker_ = std::thread{[&] {
            // std::cout << "worker io 线程启动" << std::endl;
            spdlog::info("thread of worker IO start up");
            stop_ = false;
            handler_ = std::thread{[&] {
                spdlog::info("thread of handler start up");
                HandleMessage();
            }};
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

    /// 下线
    void RpcWorker::Deconnect()
    {
        // 发送断开链接请求
        BufferPack pack;

        auto serial_num = std::to_string(Now());
        pack.push_back(MakeZmqBuffer(serial_num));
        pack.push_back(MakeZmqBuffer(RPC_WORKER));
        pack.push_back(MakeZmqBuffer(RPC_UNCONNECT));
        std::unique_lock<std::mutex> lock{services_mutex_};
        for (const auto &item : services_)
        {
            pack.push_back(MakeZmqBuffer(item.first));
        }
        lock.unlock();

        if (debug_)
        {
            // std::cout << "发送断开链接请求: " << StringifyMessages(pack) << std::endl;
            spdlog::info("Send a disconnect request is :{}", StringifyMessages(pack));
        }
        // fixme: 暴力中断，后期需要优化
        stop_ = true;
        SendAll(*socket_, pack, send_mx_);
        if (debug_)
        {
            // std::cout << "等待 broker 响应" << std::endl;
            spdlog::info("waitting response of broker");
        }
        auto res = ReceiveAll(*socket_);

        if (debug_)
        {
            // std::cout << "broker 响应 " << StringifyMessages(res) << std::endl;
            spdlog::info("the content of response of broker is : {}", StringifyMessages(res));
        }
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
    bool RpcWorker::Response(const std::string &client_uuid, const std::string &serial_num, const std::string &service_name, const std::string &body)
    {
        assert(ctx_ != nullptr);
        assert(socket_ != nullptr);
        assert(!broker_addr_.empty());

        std::vector<zmq::const_buffer> messages;
        messages.reserve(6);
        messages.push_back(MakeZmqBuffer(serial_num));
        messages.push_back(MakeZmqBuffer(RPC_WORKER));
        messages.push_back(MakeZmqBuffer(RPC_RES));
        messages.push_back(MakeZmqBuffer(client_uuid));
        messages.push_back(MakeZmqBuffer(service_name));
        messages.push_back(MakeZmqBuffer(body));

        if (debug_)
        {
            // std::cout << "响应 RPC 请求，回复内容" << StringifyMessages(messages) << std::endl;
            spdlog::info("Response to RPC request of serial_num:{} and uuid is {}, content of reply is {}", serial_num, UUID2String(client_uuid), StringifyMessages(messages));
        }
        return SendAll(*socket_, messages, send_mx_);
    }

    /// 发送消息到代理端
    void RpcWorker::SendToBroker(const std::string &header, const std::string &command, const std::string &body)
    {
        std::vector<zmq::const_buffer> messages;
        messages.reserve(5);
        auto serial_num = std::to_string(Now());
        messages.push_back(MakeZmqBuffer(serial_num));
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
            // std::cout << "发送消息到代理端 " << StringifyMessages(messages) << std::endl;
            spdlog::info("send message to endpoint of proxy, message is {}", StringifyMessages(messages));
        }

        SendAll(*socket_, messages, send_mx_);
        // TODO 错误处理
    }

    void RpcWorker::HandleMessage()
    {
        while (!stop_)
        {

            if (!messages_task_one_.empty())
            {
                // std::this_thread::sleep_for(std::chrono::milliseconds(1));
                // continue;
                std::unique_lock<std::mutex> try_lock_one(mx_task_one_, std::try_to_lock);
                if (try_lock_one.owns_lock())
                {
                    spdlog::info("handle message in messages_task_one_ on HandleMessage");
                    auto messages = std::move(messages_task_one_.front());
                    messages_task_one_.pop_front();
                    try_lock_one.unlock();

                    auto req_uuid = std::move(messages.front());
                    messages.pop_front();

                    assert(req_uuid.size() == 16 && "客户端 uuid 错误");

                    auto serial_num = std::move(messages.front());

                    uint64_t serial_num_of_request = std::stol(serial_num.to_string());

                    if (serial_num_of_request < current_max_serial_num_of_request)
                    {
                        // 如果这条小于当前处理过的最大序列号消息，说明是历史已处理消息，不重复处理，直接跳过
                        // 这里使用的是以时间戳作为序列号，后续换成其他形式的序列号需要保证单调递增
                        spdlog::info("current_max_serial_num_of_request is {}", current_max_serial_num_of_request);
                        spdlog::info("{} Is history request", serial_num_of_request);
                        continue;
                    }

                    messages.pop_front();

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

                    auto deadline_string = std::move(messages.front());
                    spdlog::info(deadline_string.to_string());
                    int64_t deadline = std::stol(deadline_string.to_string());
                    messages.pop_front();

                    auto service_name = req_service.to_string();
                    auto argumnet = req_argument.to_string();
                    std::lock_guard<std::mutex> lock{services_mutex_};
                    auto iterator = services_.find(service_name);
                    assert(iterator != services_.end() && "服务未注册");

                    std::string result = iterator->second(argumnet, deadline);
                    if (Response(req_uuid.to_string(), serial_num.to_string(), service_name, result))
                    {
                        // 处理并发送成功，并更新当前已处理最大序列号
                        spdlog::info("current_max_serial_num_of_request is {} and recevied serial num of request is {}", current_max_serial_num_of_request, serial_num_of_request);
                        current_max_serial_num_of_request = serial_num_of_request;
                        spdlog::info("response success");
                    }
                }
            }
            if (!messages_task_two_.empty())
            {
                std::unique_lock<std::mutex> try_lock_two(mx_task_two_, std::try_to_lock);
                if (try_lock_two.owns_lock())
                {
                    spdlog::info("handle message in messages_task_two_ on HandleMessage");
                    auto messages = std::move(messages_task_two_.front());
                    messages_task_two_.pop_front();
                    try_lock_two.unlock();

                    auto req_uuid = std::move(messages.front());
                    messages.pop_front();

                    assert(req_uuid.size() == 16 && "客户端 uuid 错误");

                    auto serial_num = std::move(messages.front());

                    uint64_t serial_num_of_request = std::stol(serial_num.to_string());

                    if (serial_num_of_request < current_max_serial_num_of_request)
                    {
                        // 如果这条小于当前处理过的最大序列号消息，说明是历史已处理消息，不重复处理，直接跳过
                        // 这里使用的是以时间戳作为序列号，后续换成其他形式的序列号需要保证单调递增
                        spdlog::info("current_max_serial_num_of_request is {}", current_max_serial_num_of_request);
                        spdlog::info("{} Is history request", serial_num_of_request);
                        continue;
                    }

                    messages.pop_front();

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

                    auto deadline_string = std::move(messages.front());
                    spdlog::info(deadline_string.to_string());
                    int64_t deadline = std::stol(deadline_string.to_string());
                    messages.pop_front();

                    auto service_name = req_service.to_string();
                    auto argumnet = req_argument.to_string();
                    std::lock_guard<std::mutex> lock{services_mutex_};
                    auto iterator = services_.find(service_name);
                    assert(iterator != services_.end() && "服务未注册");

                    std::string result = iterator->second(argumnet, deadline);
                    if (Response(req_uuid.to_string(), serial_num.to_string(), service_name, result))
                    {
                        // 处理并发送成功，并更新当前已处理最大序列号
                        spdlog::info("current_max_serial_num_of_request is {} and recevied serial num of request is {}", current_max_serial_num_of_request, serial_num_of_request);
                        current_max_serial_num_of_request = serial_num_of_request;
                        spdlog::info("response success");
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
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
                    spdlog::info("current_max_serial_num is {}", current_max_serial_num_of_request);
                    // std::cout << "等待 rpc 请求..." << std::endl;
                    spdlog::info("waitting request of rpc ...");
                }

                auto messages = ReceiveAll(*socket_);
                assert(messages.size() >= 4);

                if (debug_)
                {
                    // std::cout << "收到客户端的 rpc 请求：" << StringifyMessages(messages) << std::endl;
                    spdlog::info("Received an rpc request from the client: {}", StringifyMessages(messages));
                }
                

                while (true)
                {
                    std::unique_lock<std::mutex> try_lock_one(mx_task_one_, std::try_to_lock);
                    if (try_lock_one.owns_lock())
                    {
                        messages_task_one_.push_back(std::move(messages));
                        spdlog::info("push message to task list 1");
                        try_lock_one.unlock();
                        break;
                    }

                    std::unique_lock<std::mutex> try_lock_two(mx_task_two_, std::try_to_lock);
                    if (try_lock_two.owns_lock())
                    {
                        messages_task_two_.push_back(std::move(messages));
                        spdlog::info("push message to task list 2");
                        try_lock_two.unlock();
                        break;
                    }
                }

                // messages_task_one_.push_back(std::move(messages));
            }
            // 定时发送心跳包到代理端
            if (Now() >= heartbeat_at_)
            {
                SendToBroker(RPC_HB, "", "");
                heartbeat_at_ = Now() + heartbeat_;
            }
        }
    }

} // namespace mbus