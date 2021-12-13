#include <iostream>
#include <string>
#include "include/asio.hpp"
#include <cstdint>
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <mutex>
#include <deque>

#include "ConnectionManager.h"

using namespace NetworkMessages;
using asio::ip::tcp;

int clientRead(ConnectionManager& connectionManager)
{
    try
    {
		std::vector<Payload> messages;

		while (true)
		{
            connectionManager.waitForIncomingMessageFromSocket();
            Payload m;
            connectionManager.popIncomingPayload(m);
		}

	}
    catch (std::exception& e)
    {
        std::cerr << e.what() << std::endl;
    }

    return 0;
}

int clientWrite(ConnectionManager& connectionManager);

int serverPushLogic(ConnectionManager& connectionManager)
{
    connectionManager.waitForClient(13);
    
    try
    {
        int i = 0;
        while (true)
        {
            std::vector<uint8_t> v(10 * i + 1);
            v[v.size() - 1] = 1;

            connectionManager.pushOutgoingPayload(v);
            std::cout << "Pushed write message\n";
            if (true || i < 1000)
            {
                ++i;
            }
        }
    }
    catch (std::exception& e)
    {
        std::cerr << e.what() << std::endl;
    }

    return 0;
}

int serverWriteLogic(ConnectionManager& connectionManager)
{
    while (true)
    {
        connectionManager.writeOutgoingMessageToSocket();
    }
}

int clientWrite(ConnectionManager& connectionManager)
{
    try
    {
        int i = 0;
        while (true)
        {
            std::vector<uint8_t> v(10 * i + 1);
            v[v.size() - 1] = 1;


            connectionManager.pushOutgoingPayload(v);
            connectionManager.writeOutgoingMessageToSocket();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (true || i < 1000)
            {
                ++i;
            }
        }
    }
    catch (std::exception& e)
    {
        std::cerr << e.what() << std::endl;
    }

    return 0;
}

/*

int main()
{

    ConnectionManager connectionManagerServer("SERVER1");
    std::thread s1(serverPushLogic, std::ref(connectionManagerServer));
    std::thread s1Write(serverWriteLogic, std::ref(connectionManagerServer));

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    std::thread s2(clientRead, std::ref(connectionManagerServer));

    ConnectionManager connectionManagerClient1("CLIENT1");
    connectionManagerClient1.connectToServer("127.0.0.1", "13");

    std::thread t1(clientRead, std::ref(connectionManagerClient1));
    std::thread t2(clientWrite, std::ref(connectionManagerClient1));


    while(true) {}
    return 0;
}*/