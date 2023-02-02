#include "rpc/client.h"
#include "common/constants.h"
#include "common/time.h"
#include "common/uuid.h"
#include "common/zmq_helper.h"

#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>
#include <spdlog/spdlog.h>
#include <string>
#include <sys/types.h>
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
            // std::cout << "当前进程的 rpc 客户端 uuid: " << str << std::endl;
            spdlog::info("The uuid of the rpc client of the current process is {}", str);
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
            // std::cout << "连接 rpc 代理服务: " << broker_addr_ << std::endl;
            spdlog::info("connect rpc proxy service: {}", broker_addr_);
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
            // std::cout << "rpc 请求超时时间修改为: " << timeout_ << std::endl;
            spdlog::info("The timeout of rpc requests is changed to {}", timeout_);
        }
    }

    /// 设置 rpc 请求失败重试次数
    void RpcClient::SetRerties(size_t rerties)
    {
        assert(rerties >= 0);
        retries_ = rerties;
        if (debug_)
        {
            // std::cout << "rpc 请求重试次数修改为: " << timeout_ << std::endl;
            spdlog::info("The times of rerty of rpc requests is changed to {}", rerties);
        }
    }

    /// 发起阻塞等待的 rpc 请求
    std::string RpcClient::SyncCall(const std::string &service, const std::string &argv, const int64_t &timeout, const int &rerties)
    {
        assert(ctx_ != nullptr);
        assert(socket_ != nullptr);
        assert(!service.empty());
        spdlog::info("rerties is {} and timeout is {}", rerties, timeout);
        assert(rerties >= -1);
        assert(timeout >= -1);

        std::vector<zmq::const_buffer> messages;
        messages.reserve(5);
        // fixme: 暂时使用时间戳当做消息序列号
        auto serial_num = std::to_string(Now());
        messages.push_back(MakeZmqBuffer(serial_num));
        messages.push_back(MakeZmqBuffer(RPC_CLIENT));
        messages.push_back(MakeZmqBuffer(RPC_REQ));
        messages.push_back(MakeZmqBuffer(service));
        messages.push_back(MakeZmqBuffer(argv));

        // 设置请求次数
        int to_request_times = retries_;

        if (rerties == -1)
        {
            // -1 为不重试
            to_request_times = 0;
        }
        else if (rerties > 0)
        {
            // 大于0位设置实际重试次数，否则为默认重试次数
            to_request_times = rerties;
        }
        // 本身需要请求一次 加上重试一次
        to_request_times++;

        int64_t timeout_of_current_request;

        if (timeout == 0)
        {
            // 使用系统默认值
            timeout_of_current_request = timeout_;
        }
        else if (timeout == -1)
        {
            // 不设置超时，但还是设置为默认值的16倍
            timeout_of_current_request = (timeout_ << 4);
        }
        else
        {
            timeout_of_current_request = timeout;
        }
        // 设置超时时间

        while (to_request_times && ctx_)
        {
            // 将生成的deadline发送到worker方
            int64_t deadline{Now() + timeout_of_current_request};
            std::string deadline_string = std::to_string(deadline);
            spdlog::info("deadline_string is {}", deadline_string);
            messages.push_back(MakeZmqBuffer(deadline_string));

            if (debug_)
            {
                // std::cout << "准备发送消息：" << StringifyMessages(messages) << std::endl;
                spdlog::info("ready to send message: {} and serial_num is {}", StringifyMessages(messages), serial_num);
            }
            SendAll(*socket_, messages, send_mx_);
            if (debug_)
            {
                // std::cout << "发送完成" << std::endl;
                spdlog::info("send complete");
            }
        RECEIVE_AGAIN:
            if (WaitReadable(*socket_, timeout_of_current_request))
            {
                auto response = ReceiveAll(*socket_);

                auto &response_serial_num = response.front();

                if (debug_)
                {
                    // std::cout << "收到服务端响应：" << StringifyMessages(response) << std::endl;
                    spdlog::info("received server response : {} and message serial_num is {}", StringifyMessages(response), response_serial_num.to_string());
                }

                if (S1BiggersS2(serial_num, response_serial_num.to_string()))
                {
                    // 当前请求的序列号比响应回来的序列号要大，说明是历史消息，忽略掉，重新接受
                    spdlog::info("serial_num is {} and received server response serial is {}", serial_num, response_serial_num.to_string_view());
                    spdlog::info("received server response is expired");
                    goto RECEIVE_AGAIN;
                }
                response.pop_front();

                assert(response.size() >= 2 && "响应协议错误");

                auto &res_role = response.front();
                assert((Equal(res_role, RPC_WORKER) || Equal(res_role, RPC_BROKER)) && "响应协议角色错误");
                response.pop_front();

                auto &res_header = response.front();
                assert((Equal(res_header, RPC_RES) || Equal(res_header, RPC_NOTPROCESSED)) && "响应协议头错误");
                assert(!Equal(res_header, RPC_NOTPROCESSED) && "服务端未注册该服务");
                response.pop_front();

                auto &res_service = response.front();
                if (debug_)
                {
                    std::string s(res_service.data<char>(), res_service.size());
                    spdlog::info("res_service is {} and service is {}", s, service);
                }
                assert(Equal(res_service, service) && "响应服务错误");
                response.pop_front();
                assert(response.size() >= 1 && "body无内容");
                auto &res_payload = response.front();
                return res_payload.to_string();
            }
            else
            {
                if (debug_)
                {
                    if (to_request_times)
                    {
                        // std::cout << "服务端响应超时，重新连接后尝试再次请求" << std::endl;
                        spdlog::info("The server response timed out, try to request again after reconnecting");
                        // 指数退避
                        timeout_of_current_request = timeout_of_current_request << 1;
                    }
                    else
                    {
                        // std::cout << "重试次数过多，放弃请求" << std::endl;
                        spdlog::info("Too many retries, abandoning the request");
                        break;
                    }
                }
                Reconnect();
            }
            to_request_times--;
            /// 弹出deadline
            messages.pop_back();
        }
        // std::cout << "rpc 请求结束" << std::endl;
        spdlog::info("end of rpc request");
        return {};
    }

} // namespace mbus