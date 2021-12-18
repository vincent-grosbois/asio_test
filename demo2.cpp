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


using asio::ip::tcp;
/*
Payload blockUntilNextPayload(ConnectionManager& connectionManager)
{
    bool hasPayload = false;
    Payload payload;

    while (!hasPayload) {
        hasPayload = connectionManager.popIncomingPayload(payload);
    }
    return payload;
}

int serverLogic(ConnectionManager& connectionManager)
{
    connectionManager.waitForClient(13);

    Payload payload = blockUntilNextPayload(connectionManager);

    if (payload[0] == 2)
    {
        std::cout << connectionManager.getName() << ": received connection demand from client, accepted\n";
        connectionManager.pushOutgoingPayload({ 3 }); 
    }
    else
    {
        std::cout << connectionManager.getName() << ": received connection demand from client, rejected\n";
        connectionManager.pushOutgoingPayload({ 128 });
    }

    connectionManager.writeAllAndStop();
    std::cout << connectionManager.getName() << " is stopped\n";

    return 0;
}

int clientLogic(ConnectionManager& connectionManager)
{
    connectionManager.connectToServer("127.0.0.1", "13");

    connectionManager.pushOutgoingPayload({ 2 });
    Payload payload = blockUntilNextPayload(connectionManager);

    std::cout << "CLIENT received " << payload[0] << '\n';
    return 0;
}

int alwaysSendData(ConnectionManager& connectionManager)
{
    while (!connectionManager.isStopped())
    {
        connectionManager.writeOutgoingMessageToSocket();
    }

    std::cout << connectionManager.getName() << ": Stopped sending\n";
    return 0;
}

int alwaysReceiveData(ConnectionManager& connectionManager)
{
    while (!connectionManager.isStopped())
    {
        connectionManager.waitForIncomingMessageFromSocket();
    }
    std::cout << connectionManager.getName() <<  ": Stopped receiving\n";
    return 0;
}
*/

/*
int main()
{

    ConnectionManager connectionManagerServer("SERVER1");

    std::thread server(serverLogic, std::ref(connectionManagerServer));
    std::thread serverSendData(alwaysSendData, std::ref(connectionManagerServer));
    std::thread serverReceiveData(alwaysReceiveData, std::ref(connectionManagerServer));

    //std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    ConnectionManager connectionManagerClient1("CLIENT1");

    std::thread client(clientLogic, std::ref(connectionManagerClient1));
    std::thread clientSendData(alwaysSendData, std::ref(connectionManagerClient1));
    std::thread clientReceiveData(alwaysReceiveData, std::ref(connectionManagerClient1));



    // calling join in any order should be OK

    clientSendData.join();
    server.join();
    serverSendData.join();
    serverReceiveData.join();
    clientReceiveData.join();
    client.join();

    std::cout << "Everything finished correctly";

    return 0;
}*/