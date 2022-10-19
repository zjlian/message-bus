#include "rpc/client.h"
#include "common/constants.h"
#include "common/uuid.h"
#include "common/zmq_helper.h"

#include <array>
#include <cassert>
#include <chrono>
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

#include <zmq.hpp>

/*
    RPC 客户端发出的请求包格式: routing_id |  RPC_REQ | service_name | argv
    使用 zmq 的多部分消息发出
    routing_id 是 zmq socket 添加相关选项设置后自动携带的，用于区别请求的来源 
*/

namespace mbus
{

    RpcClient::RpcClient(const std::shared_ptr<zmq::context_t> &ctx)
        : ctx_(ctx), uuid_(UUID())
    {
        assert(ctx_ != nullptr);
    }

    void RpcClient::Debug()
    {
        debug_ = true;
    }

    /// 链接 rpc 服务
    void RpcClient::Connect(const std::string &broker_addr)
    {
        assert(ctx_ != nullptr);
        assert(!broker_addr.empty());

        if (debug_ && socket_ == nullptr)
        {
            auto str = UUID2String(uuid_);
            std::cout << "当前进程的 rpc 客户端 uuid: " << str << std::endl;
        }

        // NOTE: cppzmq 封装的 zmq::socket_t 在析构时会自动断开连接，所以重复调用 Connect 函数，会先断开先前的连接
        socket_ = std::make_unique<zmq::socket_t>(*ctx_, zmq::socket_type::dealer);
        assert(socket_ != nullptr && "Out Of Memory!!!");
        // 设置 socket 的标识符
        socket_->set(zmq::sockopt::routing_id, uuid_);
        // 设置 socket 关闭时丢弃所有未发送成功的消息
        socket_->set(zmq::sockopt::linger, 0);
        broker_addr_ = broker_addr;
        socket_->connect(broker_addr_);
        if (debug_)
        {
            std::cout << "连接 rpc 代理服务: " << broker_addr_ << std::endl;
        }
    }

    /// 重新连接上一次连接的代理服务
    void RpcClient::Reconnect()
    {
        assert(ctx_ != nullptr);
        assert(!broker_addr_.empty());

        Connect(broker_addr_);
    }

    /// 设置 rpc 请求超时时间
    void RpcClient::SetTimeout(size_t ms)
    {
        assert(ms >= 0);
        timeout_ = ms;
        if (debug_)
        {
            std::cout << "rpc 请求超时时间修改为: " << timeout_ << std::endl;
        }
    }

    /// 设置 rpc 请求失败重试次数
    void RpcClient::SetRerties(size_t rerties)
    {
        assert(rerties >= 0);
        retries_ = rerties;
        if (debug_)
        {
            std::cout << "rpc 请求重试次数修改为: " << timeout_ << std::endl;
        }
    }

    /// 发起阻塞等待的 rpc 请求
    std::string RpcClient::SyncCall(const std::string &service, const std::string &argv)
    {
        assert(ctx_ != nullptr);
        assert(socket_ != nullptr);
        assert(!service.empty());

        std::vector<zmq::const_buffer> messages;
        messages.reserve(3);
        messages.push_back(MakeZmqBuffer(RPC_CLIENT));
        messages.push_back(MakeZmqBuffer(RPC_REQ));
        messages.push_back(MakeZmqBuffer(service));
        messages.push_back(MakeZmqBuffer(argv));

        size_t retries_left = retries_;
        while (retries_left && ctx_)
        {
            if (debug_)
            {
                std::cout << "准备发送消息：" << StringifyMessages(messages) << std::endl;
            }
            SendAll(*socket_, messages);
            if (debug_)
            {
                std::cout << "发送完成" << std::endl;
            }

            if (WaitReadable(*socket_, timeout_))
            {
                auto response = ReceiveAll(*socket_);
                if (debug_)
                {
                    std::cout << "收到服务端响应：" << StringifyMessages(response) << std::endl;
                }

                assert(response.size() >= 3 && "响应协议错误");

                auto &res_role = response.front();
                assert(Equal(res_role, RPC_WORKER) && "响应协议错误");
                response.pop_front();

                auto &res_header = response.front();
                assert(Equal(res_header, RPC_RES) && "响应协议错误");
                response.pop_front();

                auto &res_service = response.front();
                assert(Equal(res_service, service) && "响应服务错误");
                response.pop_front();

                auto &res_payload = response.front();
                return res_payload.to_string();
            }
            else
            {
                retries_left--;
                if (debug_)
                {
                    if (retries_left)
                    {
                        std::cout << "服务端响应超时，重新连接后尝试再次请求" << std::endl;
                    }
                    else
                    {
                        std::cout << "重试次数过多，放弃请求" << std::endl;
                        break;
                    }
                }
                Reconnect();
            }
        }
        std::cout << "rpc 请求结束" << std::endl;
        return {};
    }

} // namespace mbus