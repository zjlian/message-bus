#include "common/time.h"
#include "common/zmq_helper.h"
#include "rpc/client.h"
#include "rpc/remote_caller.h"
#include "rpc/worker.h"
#include "tests/add_service.pb.h"

#include <chrono>
#include <memory>
#include <spdlog/spdlog.h>
#include <thread>
#include <vector>

#include <unistd.h>
#include <zmq.hpp>

/**
 * rpc worker 实现实例，实现接收两个数值，返回给 client 相加后的结果
*/

int main()
{
    auto ctx = std::make_shared<zmq::context_t>(1);
    mbus::RemoteCaller caller{ctx};

    caller.Debug();

    auto add_service = [](value::AddService argv, value::GeneralMessage general_message, const uint64_t &deadline) {
        spdlog::info("number1 is {} and number2 is {}", argv.number1(), argv.number2());
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        argv.set_result(argv.number1() + argv.number2());
        spdlog::info("deadline is {}", deadline);
        if (deadline < mbus::Now())
        {
            spdlog::info("already timeout");
        }
        return argv;
    };
    // 第一个模板参数是接收的 rpc 请求的参数的类型，第二格模板参数是响应 rpc 请求的类型
    caller.RegisterService<value::AddService, value::AddService>("add", add_service);

    caller.WorkerConnect("tcp://localhost:5555");
    pause();
    std::this_thread::sleep_for(std::chrono::milliseconds(10000));
    caller.Deconnect();
    std::cout << "下线" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    // pause();

    // mbus::RpcWorker worker{ctx};

    // worker.Debug();

    // worker.RegisterService("echo", [](const std::string &res) -> std::string {
    //     std::cout << "echo -> " << res << std::endl;
    //     return res;
    // });

    // worker.RegisterService("echo2", [](const std::string &res) -> std::string {
    //     auto echo = res + res;
    //     std::cout << "echo -> " << echo << std::endl;
    //     return echo;
    // });

    // worker.Connect("tcp://localhost:5555");

    // while (true)
    // {
    //     sleep(1);
    // }
}