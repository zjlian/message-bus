#include "general_message.pb.h"
#include "mbus/client.h"
#include "mbus/message_dealer.h"
#include "tests/add_service.pb.h"

#include <string_view>
#include <unistd.h>

void PrintMessage(value::GeneralMessage general_message)
{
    std::cout << "收到了：" << general_message.payload() << std::endl;
}

int main(int argc, const char **args)
{
    auto ctx = std::make_shared<zmq::context_t>(1);
    mbus::MessageDealer dealer{ctx};
    dealer.Connect("tcp://localhost:54321", "tcp://localhost:12345");

    dealer.AddTopicListener<std::string>(
        "/hello",
        [](std::string msg, value::GeneralMessage general_message) {
            std::cout << msg << std::endl;
        });

    dealer.AddTopicListener<value::Any>(
        "/world",
        [](value::Any msg, value::GeneralMessage general_message) {
            std::cout << msg.data() << std::endl;
        });

    pause();

    // // 创建一个消息总线客户端
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

    // // 订阅话题 /hello
    // // 当收到新消息后会在后台线程自动执行注册的 PrintMessage 函数
    // client.Subscribe("hello", PrintMessage);

    // // 干别的事
    // while (true)
    // {
    //     std::this_thread::sleep_for(std::chrono::seconds(1));
    // }
}