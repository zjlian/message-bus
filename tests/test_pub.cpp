#include "message_bus/client.h"
#include <string>
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
    auto client = mbus::Client{ctx};
    // 连接消息总线服务
    if (argc >= 2 && std::string{args[1]} == std::string{"server"})
    {
        std::cout << "连接服务器" << std::endl;
        client.Connect("tcp://localhost:4321", "tcp://localhost:1234");
    }
    else
    {
        client.ConnectOnLocalMode(4321, 1234);
    }
    size_t count = 0;
    auto pid = getpid();
    while (count < 100000)
    {
        count++;

        sengMsg(client, count, pid, "a", "hello a");
        sengMsg(client, count, pid, "b", "hello b");
        sengMsg(client, count, pid, "c", "hello c");
        sengMsg(client, count, pid, "d", "hello d");
        sengMsg(client, count, pid, "e", "hello e");
        sengMsg(client, count, pid, "f", "hello f");

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
