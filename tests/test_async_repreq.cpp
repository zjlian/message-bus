#include "general_message.pb.h"
#include "message_bus/bus.h"
#include "message_bus/client.h"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include <gflags/gflags.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>
#include <zmq.hpp>

constexpr auto SERVER_CONNECT_ADDR = "tcp://localhost:5570";
constexpr auto SERVER_BIND_ADDR = "tcp://*:5570";

std::array<char, 10> UUID()
{
    timeval t;
    gettimeofday(&t, nullptr);
    srandom(t.tv_sec + t.tv_usec + syscall(SYS_gettid));
    std::array<char, 10> id;
    sprintf(id.data(), "%04lX-%04lX", random() % 0x10000, random() % 0x10000);
    return id;
}

/// 接收 zmq 的全部分块消息
std::vector<zmq::message_t> ReceiveAll(zmq::socket_t &socket)
{
    std::vector<zmq::message_t> message;
    do
    {
        zmq::message_t msg;
        auto result = socket.recv(msg, zmq::recv_flags::none);
        if (result.has_value())
        {
            message.emplace_back(std::move(msg));
        }
    } while (socket.get(zmq::sockopt::rcvmore));

    return message;
}

void PrintMessage(const std::vector<zmq::message_t> &msgs)
{
    for (const auto &m : msgs)
    {
        std::cout << std::string_view{static_cast<const char *>(m.data()), m.size()}
                  << " | ";
    }
    std::cout << std::endl;
}

void Client()
{
    auto id = UUID();
    std::string_view id_sv{id.data()};
    auto ctx = zmq::context_t{1};
    auto client = zmq::socket_t{ctx, zmq::socket_type::dealer};
    client.set(zmq::sockopt::routing_id, id.data());
    client.connect(SERVER_CONNECT_ADDR);
    std::cout << std::string_view{id.data()} << std::endl;

    std::vector<zmq::pollitem_t> items;
    items.push_back({client.handle(), 0, ZMQ_POLLIN, 0});

    size_t request_number = 0;
    while (true)
    {
        for (size_t tick = 0; tick < 100; tick++)
        {

            auto poll_result = zmq::poll(items, std::chrono::milliseconds(10));
            assert(poll_result >= 0);
            if (poll_result == 0)
            {
                continue;
            }
            for (auto &item : items)
            {
                if (item.events & ZMQ_POLLIN)
                {
                    auto msgs = ReceiveAll(client);
                    std::cout << id_sv << " 收到服务端的回复: ";
                    PrintMessage(msgs);
                }
            }
        }

        request_number++;
        std::string str{"request #"};
        str += std::to_string(request_number);
        std::cout << "客户端发送：" << str << std::endl;
        client.send(zmq::message_t{str.data(), str.size()}, zmq::send_flags::none);
    }
}

void ServerWorker(std::string ipc_addr)
{
    std::cout << "worker 启动" << std::endl;
    auto id = UUID();
    std::string_view id_sv{id.data()};
    auto ctx = zmq::context_t{1};
    auto socket = zmq::socket_t{ctx, zmq::socket_type::dealer};
    socket.connect(ipc_addr);
    // socket.connect("tcp://localhost:5555");

    while (true)
    {
        std::cout << "worker 等待消息" << std::endl;
        auto message = ReceiveAll(socket);
        assert(message.size() >= 2);
        std::string_view client_uuid{message[0].data<char>(), message[0].size()};
        std::string_view client_message{message[1].data<char>(), message[1].size()};
        assert(!client_message.empty());
        std::cout << "收到来自客户端的消息：";
        PrintMessage(message);

        size_t replise = random() % 5;
        for (size_t reply = 0; reply < replise; reply++)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds((random() % 1000) + 1));
            zmq::const_buffer buffer{client_uuid.data(), client_uuid.size()};
            socket.send(buffer, zmq::send_flags::sndmore);
            zmq::const_buffer content{client_message.data(), client_message.size()};
            socket.send(content, zmq::send_flags::sndmore);
            socket.send(buffer);
        }
    }
}

void Server()
{
    auto id = UUID();
    std::string_view id_sv{id.data()};
    auto ctx = zmq::context_t{1};

    auto frontend = zmq::socket_t{ctx, zmq::socket_type::router};
    frontend.bind(SERVER_BIND_ADDR);

    auto backend = zmq::socket_t{ctx, zmq::socket_type::dealer};
    // 让系统自动分配端口号
    std::string backend_addr{"tcp://*:0"};
    backend.bind(backend_addr);
    // 获取实际 bind 的地址
    backend_addr = backend.get(zmq::sockopt::last_endpoint);

    auto core_size = std::thread::hardware_concurrency();
    // auto core_size = 1;
    std::vector<std::thread> worker;
    for (size_t i = 0; i < core_size; i++)
    {
        worker.emplace_back(ServerWorker, backend_addr);
    }
    std::cout << "proxy 启动" << std::endl;
    zmq::proxy(frontend, backend);
    for (auto &thr : worker)
    {
        if (thr.joinable())
        {
            thr.join();
        }
    }
}

int main(int argc, char *argv[])
{
    std::thread client1{Client};
    std::thread client2{Client};
    std::thread client3{Client};
    std::thread server{Server};

    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
}
