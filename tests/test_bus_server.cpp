#include "message_bus/bus.h"

#include <chrono>
#include <iostream>
#include <thread>

int main(int, const char **)
{
    auto ctx = std::make_shared<zmq::context_t>(3);
    auto bus = mbus::Bus{ctx};
    bus.Run("tcp://*", 1234, 4321);

    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds{1});
    }
    return 0;
}