#include "message_bus/client.h"

#include <string_view>

int times = 0;
void PrintMessage(value::GeneralMessage general_message)
{
    if (times < 100)
    {
        std::cout << "收到了：" << general_message.payload() << std::endl;
        times++;
    }
}

int main(int argc, const char **args)
{
    auto ctx = std::make_shared<zmq::context_t>(1);
    // 创建一个消息总线客户端
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

    // 订阅话题 /hello
    // 当收到新消息后会在后台线程自动执行注册的 PrintMessage 函数
    client.Subscribe("a", PrintMessage);
    client.Subscribe("b", PrintMessage);
    client.Subscribe("c", PrintMessage);
    client.Subscribe("d", PrintMessage);
    client.Subscribe("e", PrintMessage);
    client.Subscribe("f", PrintMessage);

    // 干别的事
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}