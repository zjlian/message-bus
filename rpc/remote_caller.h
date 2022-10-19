#pragma once

#include "common/make_general_message.h"
#include "general_message.pb.h"
#include "rpc/client.h"
#include "rpc/worker.h"

#include <atomic>
#include <cassert>
#include <functional>
#include <memory>
#include <mutex>

#include <zmq.hpp>

namespace mbus
{

    /// RPC 请求函数的返回值类型
    template <typename PayloadType>
    struct ResultPack
    {
        // 未解的原始消息，附带来源和发送时间等信息
        value::GeneralMessage general_message;
        // 反序列后的 general_message.payload，存储实际的 RPC
        PayloadType payload;
    };

    /// RPC 服务客户端整合工具类，合并了 RpcWorker 和 RpcClient，方便使用
    class RemoteCaller
    {
    public:
        RemoteCaller(const std::shared_ptr<zmq::context_t> &ctx)
            : ctx_(ctx), rpc_worker_(ctx_), rpc_client_(ctx_)
        {
        }

        void Debug()
        {
            rpc_worker_.Debug();
            rpc_client_.Debug();
        }

        /// @brief 注册 RPC 服务
        /// @param ReceiveType 接收 RPC 请求后，参数解析的目标类型
        /// @param ResponseType RPC 请求处理函数的返回值类型
        /// @param service_name 服务名
        /// @param callback RPC 请求处理函数
        template <typename ReceiveType, typename ResponseType>
        void RegisterService(
            const std::string &service_name,
            const std::function<ResponseType(ReceiveType, value::GeneralMessage)> &callback)
        {
            assert(!connected);

            // RPC 请求处理函数的解包封包包装
            auto unpack_wrapper = [callback](const std::string &msg) {
                assert(!msg.empty() && "收到的请求内容为空");

                value::GeneralMessage general_message;
                bool parse_success = general_message.ParseFromString(msg);
                assert(parse_success && "收到的请求协议错误，无法解析");

                // 解析 general_message 的 payload 部分到指定类型
                auto argv = UnmakeGeneralMessage<ReceiveType>(general_message);
                ResponseType result = callback(std::move(argv), std::move(general_message));
                // 封包 result 到 GeneralMessage，并序列化返回给客户端
                auto message_proto = MakeGeneralMessage(result);
                std::string message;
                message_proto.SerializeToString(&message);
                return message;
            };

            rpc_worker_.RegisterService(service_name, unpack_wrapper);
        }

        /// @brief 链接 RPC 代理服务
        /// @param broker_addr  zmq socket 的地址
        void Connect(const std::string &broker_addr)
        {
            assert(ctx_ != nullptr);

            rpc_worker_.Connect(broker_addr);
            rpc_client_.Connect(broker_addr);

            connected = true;
        }

        /// @brief 发起同步阻塞的 RPC 请求
        /// @thread-safe
        template <typename ArgementType, typename ResultType>
        ResultPack<ResultType> SyncCall(const std::string &service_name, const ArgementType &argv)
        {
            auto argv_proto = MakeGeneralMessage(argv);
            std::string message;
            argv_proto.SerializeToString(&message);
            std::unique_lock<std::mutex> lock{rpc_client_mutex_};
            auto response = rpc_client_.SyncCall(service_name, message);
            lock.unlock();
            // assert(response.size() > 0);
            std::cout << "responce size " << response.size() << std::endl;
            std::cout << "responce " << response << std::endl;
            ResultPack<ResultType> result;
            bool success = result.general_message.ParseFromString(response);
            assert(success && "服务端响应协议错误");

            result.payload = UnmakeGeneralMessage<ResultType>(result.general_message);
            return result;
        }

    private:
        std::atomic<bool> connected{false};
        std::shared_ptr<zmq::context_t> ctx_{nullptr};
        RpcWorker rpc_worker_;
        std::mutex rpc_client_mutex_{};
        RpcClient rpc_client_;
    };

} // namespace mbus
