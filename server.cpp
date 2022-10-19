#include "common/zmq_helper.h"
#include "general_message.pb.h"
#include "message_bus/bus.h"
#include "message_bus/client.h"
#include "rpc/broker.h"
#include "rpc/client.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#include <gflags/gflags.h>
#include <unistd.h>
#include <zmq.hpp>

DEFINE_int32(pub_port, 12345, "客户端消息发布需要连接的端口");
DEFINE_int32(sub_port, 54321, "客户端消息订阅需要连接的端口");

int main(int argc, char *argv[])
{
    auto ctx = std::make_shared<zmq::context_t>(3);
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    // 主进程逻辑，创建消息总线服务
    mbus::Bus bus{ctx};
    bus.Run("tcp://*", FLAGS_pub_port, FLAGS_sub_port);

    std::thread rpc_broker_worker{[&] {
        mbus::RpcBroker rpc_broker{ctx};
        rpc_broker.Debug();
        rpc_broker.Bind("tcp://*:5555");
        rpc_broker.Run();
    }};

    pause();

    rpc_broker_worker.join();

    return 0;
}
