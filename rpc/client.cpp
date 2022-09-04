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

namespace mbus
{

    RpcClient::RpcClient(const std::shared_ptr<zmq::context_t> &ctx)
        : ctx_(ctx), uuid_(UUID())
    {
        assert(ctx_ != nullptr);
        auto str = UUID2String(uuid_);
        std::cout << str << std::endl;
    }

    bool RpcClient::WaitReadable(zmq::socket_t &socket)
    {
        std::array<zmq::pollitem_t, 1> items{{socket.handle(), 0, ZMQ_POLLIN, 0}};
        zmq::poll(items, std::chrono::milliseconds(timeout_));
        return items[0].revents & ZMQ_POLLIN;
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

        // NOTE: cppzmq 封装的 zmq::socket_t 在析构时会自动断开连接，所以重复调用 Connect 函数，如果有，会先断开先前的连接
        socket_ = std::make_unique<zmq::socket_t>(*ctx_, zmq::socket_type::dealer);
        assert(socket_ != nullptr && "Out Of Memory!!!");
        socket_->set(zmq::sockopt::routing_id, uuid_);
        broker_addr_ = broker_addr;
        socket_->connect(broker_addr_);
        if (debug_)
        {
            std::cout << "连接 rpc 服务: " << broker_addr_ << std::endl;
        }
    }

    /// 设置 rpc 请求超时时间
    void RpcClient::SetTimeout(size_t ms)
    {
        assert(ms >= 0);
        timeout_ = ms;
    }

    /// 设置 rpc 请求失败重试次数
    void RpcClient::SetRerties(size_t rerties)
    {
        assert(rerties >= 0);
        retries_ = rerties;
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

            if (WaitReadable(*socket_))
            {
                auto response = ReceiveAll(*socket_);
                if (debug_)
                {
                    std::cout << "收到服务端响应：" << StringifyMessages(response) << std::endl;
                }

                assert(response.size() >= 3 || "响应协议错误");

                auto &res_header = response.front();
                assert(memcmp(res_header.data<char>(), RPC_CLIENT, strlen(RPC_CLIENT)) || "响应协议错误");
                response.pop_front();

                auto &res_service = response.front();
                assert(memcmp(res_service.data<char>(), service.data(), service.size()) || "响应服务错误");
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
                Connect(broker_addr_);
            }
        }

        return {};
    }

} // namespace mbus