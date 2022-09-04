#include "general_message.pb.h"
#include "message_bus/bus.h"
#include "message_bus/client.h"
#include "rpc/client.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#include <gflags/gflags.h>
#include <unistd.h>
#include <zmq.hpp>

DEFINE_int32(pub_port, 4567, "客户端消息发布需要连接的端口");
DEFINE_int32(sub_port, 4568, "客户端消息订阅需要连接的端口");

int main(int argc, char *argv[])
{
    auto ctx = std::make_shared<zmq::context_t>(3);
    mbus::RpcClient rpc{ctx};
    return 0;
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    // 主进程逻辑，创建消息总线服务
    mbus::Bus bus{ctx};
    bus.Run("tcp://*", FLAGS_pub_port, FLAGS_sub_port);

    pause();
    return 0;
}
