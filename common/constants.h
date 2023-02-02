#pragma once

#include <cstddef>

namespace mbus
{

    constexpr const char *RPC_CLIENT = "client";

    constexpr const char *RPC_WORKER = "worker";

    constexpr const char *RPC_BROKER = "broker";

    // client 端请求的协议头
    constexpr const char *RPC_REQ = "rpcreq";

    // client 端查询服务是否存在的协议头
    constexpr const char *RPC_QUERY = "rpcqry";

    // worker 端响应的协议头
    constexpr const char *RPC_RES = "rpcres";

    // worker 端给 broker 端发送心跳包的协议头
    constexpr const char *RPC_HB = "rpchbt";

    // worker 端给 broker 端发送服务注册请求的协议头
    constexpr const char *RPC_REG = "rpcreg";

    // worker 端给 broker 端发送 RPC 响应结果的协议头
    // constexpr const char *RPC_REPLY = "rpcrep";

    // worker 端给 broker 端发送下线请求的协议头
    constexpr const char *RPC_UNCONNECT = "rpcunc";

    // broker 端给 worker 端发送的请求处理成功的协议头
    constexpr const char *RPC_SUCCESS = "rpcsuc";

    // broker 端给 client 端发送的请求处理失败的协议头
    constexpr const char *RPC_NOTPROCESSED = "rpcunprocessed";

    // 心跳包超时等待次数(4*2500 = 10秒)
    constexpr size_t HEARTBEAT_LIVENESS = 4;

    // 心跳包等待间隔，单位毫秒
    constexpr size_t HEARTBEAT_INTERVAL = 2500;

    // 心跳包最终认定超时的时间，单位毫秒
    constexpr size_t HEARTBEAT_EXPIRY = HEARTBEAT_INTERVAL * HEARTBEAT_LIVENESS;

} // namespace mbus