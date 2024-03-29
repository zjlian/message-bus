#include "mbus/client.h"
#include "mbus/message_dealer.h"
#include "tests/add_service.pb.h"
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <unistd.h>

void sengMsg(mbus::Client &client, int count, int pid, std::string topic, std::string msg)
{
    msg += std::to_string(count);
    msg += ":";
    msg += std::to_string(pid);
    // 发送消息到话题 /hello，目前支持直接传参任意字符串类型和 proto 对象实例
    client.Publish(topic, msg);
    std::cout << "发送消息" << count << std::endl;
}

int main(int argc, const char **args)
{
    auto ctx = std::make_shared<zmq::context_t>(1);
    mbus::MessageDealer dealer{ctx};
    dealer.Connect("tcp://localhost:54321", "tcp://localhost:12345");

    while (true)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // 随机向话题 /hello 或 /world 发布消息
        if ((random() % 100) >= 50)
        {
            dealer.Publish("/hello", "hello");
            std::cout << "发布 hello 到 /hello" << std::endl;
        }
        else
        {
            value::Any world;
            world.set_data("world");
            dealer.Publish("/world", world);
            std::cout << "发布 world 到 /world" << std::endl;
        }
    }

    // auto ctx = std::make_shared<zmq::context_t>(1);
    // auto client = mbus::Client{ctx};
    // // 连接消息总线服务
    // if (argc >= 2 && std::string{args[1]} == std::string{"server"})
    // {
    //     std::cout << "连接服务器" << std::endl;
    //     client.Connect("tcp://localhost:4321", "tcp://localhost:1234");
    // }
    // else
    // {
    //     client.ConnectOnLocalMode(4321, 1234);
    // }
    // size_t count = 0;
    // auto pid = getpid();
    // while (count < 100000)
    // {
    //     count++;

    //     sengMsg(client, count, pid, "a", "hello a");
    //     sengMsg(client, count, pid, "b", "hello b");
    //     sengMsg(client, count, pid, "c", "hello c");
    //     sengMsg(client, count, pid, "d", "hello d");
    //     sengMsg(client, count, pid, "e", "hello e");
    //     sengMsg(client, count, pid, "f", "hello f");

    //     std::this_thread::sleep_for(std::chrono::seconds(1));
    // }
}
