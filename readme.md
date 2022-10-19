# 消息总线模式的进程间通信库

## 需求
1. 支持配置文件或命令启动参数，配置某些功能的开关
2. 发布订阅模式，支持话题
3. 嵌入模式运行，简单用法，仅支持单机多进程通信。使用 message-bus 的进程谁先启动，谁就是中心代理
4. 独立服务模式运行，单独运行的 message-bus 服务程序，支持跨网络通信
5. 独立服务模式支持记录消息日志，需要异步记录，尽量不影响消息收发的延迟
6. 独立服务模式需要一定的可用性保证，服务崩溃对系统中节点通信的影响尽可能少
7. 不考虑消息收发的可靠性，该通信库仅用于定时发布的各种传感器信息和状态信息

## 依赖
1. **zmq**，基于 zmq 实现通信
1. **cppzmq**，c++ 封装的 zmq 接口
2. **protobuf**，消息序列化
3. **gflag**，命令行参数解析
4. **gtest**，单元测试

## 后续优化
- [ ] 降低消息接收延迟，消息接收线程收到新消息，将消息序列化和回调函数放入到线程池中执行，避免消息接收线程阻塞造成后续消息延迟。
- [ ] 内存优化，使用 buffer 对象池或内存池，避免大量动态内存分配造成的延迟和 cpu 消耗


## 使用例子
目前 message-bus 支持发布订阅模式和 RPC 模式两种通信方式。    

无论是发布订阅还是 RPC，都需要启动一个中心代理服务 `message-bus-server`，本项目编译后能在 `build/` 目录下看到这个程序。

对于发布订阅，中心代理服务就是简单封装一下 zmq 的 zmq_proxy，实现可以任意动态拓展发布订阅端程序。

对于 RPC，中心代理服务区分了 worker 和 client 两种角色，中心代理服务对两者提供了 服务注册、心跳包存活监控、RPC 请求分发等功能。worker 负责处理来自 client 的 RPC 请求，请求和响应的消息包，都由中心代理服务负责转发。 

### 项目引入方式
需要用 message-bus 进行进程间通信的项目，主要使用 cmake 提供 FetchContent 模块引入。
在项目中的 CMakeLists.txt 文件中添加如下代码:
```cmake
# 拉公司内部仓库的项目
message(STATUS "正在下载公司内部仓库源码，如果需要请输入帐号密码")
include(FetchContent)
# 指定
FetchContent_Declare(
  message_bus
  GIT_REPOSITORY git@github.com:zjlian/message-bus.git
)
FetchContent_MakeAvailable(message_bus)
message(STATUS "============= 仓库下载完成 ============= ") 
```
然后给需要用这个库的 target 添加相关的依赖配置就可以了：
```cmake
target_include_directories(${PROJECT_NAME} PUBLIC message_bus mbus_rpc)
target_link_libraries(${PROJECT_NAME} PUBLIC message_bus mbus_rpc)
```


### 发布订阅模式
由于 zmq_proxy 的实现，发布订阅模式的发布端和订阅段需要分别连接两个不同的端口。

发布订阅模式主要使用头文件 "mbus/message_dealer.h" 下的 mbus::MessageDealer 类进行。

下面是一个简单的发布订阅使用例子，发布端程序分别向话题 /hello 和 /world 发布消息，话题 /hello 发布的是普通的字符串消息，话题 /world 发布的是 proto 消息。

发布端程序
```c++
#include "mbus/message_dealer.h"
#include "tests/add_service.pb.h"

#include <thread>
#include <iostream>

int main(int argc, const char **args)
{
    auto ctx = std::make_shared<zmq::context_t>(1);
    mbus::MessageDealer dealer{ctx};
    // 连接中心代理服务的发布订阅端口
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
}
```

订阅端的程序
```c++
#include "mbus/message_dealer.h"
#include "tests/add_service.pb.h"

#include <iostream>

int main(int argc, const char **args)
{
    auto ctx = std::make_shared<zmq::context_t>(1);
    mbus::MessageDealer dealer{ctx};
    // 连接中心代理服务的发布订阅端口
    dealer.Connect("tcp://localhost:54321", "tcp://localhost:12345");

    // 订阅话题 /hello，并指定接收到的消息解析成 std::string 类型
    dealer.AddTopicListener<std::string>(
        "/hello",
        [](std::string msg, value::GeneralMessage general_message) {
            std::cout << msg << std::endl;
        });

    // 订阅话题 /world，并指定接收到的消息解析成 value::Any 类型
    dealer.AddTopicListener<value::Any>(
        "/world",
        [](value::Any msg, value::GeneralMessage general_message) {
            std::cout << msg.data() << std::endl;   
        });

    pause();
}
```

### RPC 模式
RPC 模式下，客户端分为两种角色，分别是 worker 和 client，worker 对外提供服务，client 通过服务名称发起请求，中心代理服务会将请求分发给对应的 worker 处理。

RPC 模式主要使用头文件 "rpc/remote_caller.h" 下的 mbus::RemoteCaller 类进行。

下面是使用 RPC 模式实现跨进程计算的例子，client 进程发起 RPC 请求计算两数相加，由 worker 进程接收处理并返回结果。

protobuf
```proto 
syntax = "proto3";
package value;

message AddService
{
    // 需要计算的两个数值
    int32 number1 = 1;
    int32 number2 = 2;

    // 计算结果
    int32 result = 3;
}
```

worker 进程的代码
```c++
#include "rpc/remote_caller.h"
#include "tests/add_service.pb.h"

#include <memory>
#include <zmq.hpp>

int main()
{
    auto ctx = std::make_shared<zmq::context_t>(1);
    mbus::RemoteCaller caller{ctx};

    auto add_service = [](value::AddService argv, value::GeneralMessage general_message) {
        argv.set_result(argv.number1() + argv.number2());
        return argv;
    };
    // 第一个模板参数是接收的 rpc 请求的参数的类型，第二个模板参数是响应 rpc 请求的类型
    caller.RegisterService<value::AddService, value::AddService>("add", add_service);

    caller.Connect("tcp://localhost:5555");

    // 暂停主线程，避免程序直接结束
    pause();
}
```

client 进程的代码
```c++
#include "rpc/remote_caller.h"
#include "tests/add_service.pb.h"

#include <memory>
#include <zmq.hpp>

int main()
{
    auto ctx = std::make_shared<zmq::context_t>(1);
    mbus::RemoteCaller caller{ctx};
    caller.Connect("tcp://localhost:5555");

    int32_t i = 0;
    while (true)
    {
        value::AddService add;
        add.set_number1(i);
        add.set_number2(i);

        std::cout << "RPC 请求计算 " << i << " + " << i << std::endl;

        // 第一个模板参数是发送的 rpc 请求参数的类型，第二个模板参数是 rpc 请求响应的类型
        auto result = caller.SyncCall<value::AddService, value::AddService>("add", add);

        std::cout << "计算结果 " << result.payload.result() << std::endl
                  << std::endl;

        i++;
        sleep(1);
    }
}
```