#include <iostream>
#include <string>
//#include <asio.hpp>
#include "include/asio.hpp"
#include <cstdint>

using asio::ip::tcp;

#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <mutex>
#include <deque>

#include "ConnectionManager.h"

int client()
{
    try
    {
        ConnectionManager connectionManager("CLIENT");

        connectionManager.connectToServer("127.0.0.1", "13");


		std::vector<NetworkMessage> messages;

		while (true)
		{
            connectionManager.waitForIncomingMessageFromSocket();
            NetworkMessage m;
            connectionManager.popIncomingMessage(m);
            //assert(m[m.size() - 2] == 0);
            //assert(m[m.size() - 1] == 1);
		}

	}
    catch (std::exception& e)
    {
        std::cerr << e.what() << std::endl;
    }

    return 0;
}

int server()
{
    try
    {
        ConnectionManager connectionManager("SERVER");

        connectionManager.waitForClient(13);
        
        int i = 0;
        while(i < 200)
        {
            std::vector<uint8_t> v(10*i + 1);
            v[v.size() - 1] = 1;

            auto m = NetworkMessages::MakeMessage(v);

            connectionManager.postOutgoingMessage(m);
            connectionManager.writeOutgoingMessageToSocket();
            ++i;
        }
        while (0) {
            auto v = std::vector<uint8_t>(100);
            auto m = NetworkMessages::MakeMessage(v);
            connectionManager.postOutgoingMessage(m);
            connectionManager.writeOutgoingMessageToSocket();
        };
    }
    catch (std::exception& e)
    {
        std::cerr << e.what() << std::endl;
    }

    return 0;
}

int main()
{


    std::thread first(server);     // spawn new thread that calls foo()
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    std::thread second(client);
    while(true) {}
    return 0;
}