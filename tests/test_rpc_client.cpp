#include "rpc/remote_caller.h"
#include "tests/add_service.pb.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <spdlog/spdlog.h>
#include <string_view>

#include <thread>
#include <unistd.h>
#include <zmq.hpp>

void add(mbus::RemoteCaller &caller, const std::string name, int32_t i)
{
    
    while (true)
    {
        value::AddService add;
        add.set_number1(i);
        add.set_number2(i);

        // std::cout << name << " RPC 请求计算 " << i << " + " << i << std::endl;
        spdlog::warn("{} 请求计算 {} + {}", name, i, i);

        // 第一个模板参数是发送的 rpc 请求参数的类型，第二格模板参数是 rpc 请求响应的类型
        auto result = caller.SyncCall<value::AddService, value::AddService>("add", add, 200000, 10);

        // std::cout << name << " 计算结果 " << result.payload.result() << std::endl
        //           << std::endl;
        spdlog::warn("{} 的计算结果是 {}", name, result.payload.result());
        i++;
        // sleep(1);
    }
}

int main(int argc, const char **args)
{
    auto ctx = std::make_shared<zmq::context_t>(1);
    mbus::RemoteCaller caller{ctx};
    caller.Debug();
    caller.ClientConnect("tcp://localhost:5555");
    std::thread a{[&] {
        add(caller, "a", 100);
    }};
    std::thread b{[&] {
        add(caller, "b", 200);
    }};
    std::thread c{[&] {
        add(caller, "c", 300);
    }};
    std::thread d{[&] {
        add(caller, "d", 400);
    }};
    std::thread e{[&] {
        add(caller, "e", 500);
    }};
    pause();

    // mbus::RpcClient client{ctx};
    // client.Debug();
    // client.Connect("tcp://localhost:5555");

    // auto result = client.SyncCall("echo", "sbsb");
    // std::cout << result << std::endl;
    // result = client.SyncCall("echo2", "sbsb");
    // std::cout << result << std::endl;
}