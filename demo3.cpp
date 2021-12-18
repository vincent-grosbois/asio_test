#include <iostream>
#include <string>
#include "include/asio.hpp"



#include "ConnectionManager.h"

using asio::ip::tcp;

#include "NetworkMessage.h"

int server()
{
	asio::io_context io_context_server;
	asio::io_context io_context_queue;

	asio::executor_work_guard<asio::io_context::executor_type> work(io_context_server.get_executor());
	asio::executor_work_guard<asio::io_context::executor_type> work2(io_context_queue.get_executor());

	std::thread io_server_t1([&io_context_server]() { io_context_server.run(); });
	std::thread io_server_t2([&io_context_queue]() { io_context_queue.run(); });


	NetworkConnection server("Server", io_context_server, io_context_server);

	server.acceptConnectionSync(13);

	/*
	for (int i = 1; i < 3+1; ++i)
	{
		std::vector<uint8_t> p;
		p.resize(i);
		p[p.size() - 1] = 4;
		server.write(std::move(p));
	}

	std::cout << "Server finished writing\n";*/

	std::vector<uint8_t> msg;
	server.getNextPayload(msg);

	IncomingNetworkMessage msg_(std::move(msg));

	if (msg_.getMessageType() == MessageType::Handshake1)
	{
		server.write(std::move(CreateHandshake2Message()));
	}
	else {
		std::cout << "Incorrect message received from client\n";
	}

	server.getNextPayload(msg);
	IncomingNetworkMessage msg_2(std::move(msg));
	if (msg_2.getMessageType() == MessageType::Handshake3)
	{
		std::cout << "Server Handshake completed\n";
	}
	else {
		std::cout << "Incorrect message received from client\n";
	}

	server.close();

	work.reset();
	work2.reset();

	io_server_t1.join();
	io_server_t2.join();
	std::cout << "Server threads joined\n";

	return 0;
}

int client()
{
	tcp::endpoint endpoint(asio::ip::address::from_string("127.0.0.1"), 13);

	asio::io_context io_context_client_socket;
	asio::io_context io_context2;

	NetworkConnection client("Client", io_context_client_socket, io_context2);
	client.connectSync(endpoint);

	asio::executor_work_guard<asio::io_context::executor_type> work(io_context_client_socket.get_executor());
	asio::executor_work_guard<asio::io_context::executor_type> work2(io_context2.get_executor());

	
	// use 2 threads for managing the connection io_context
	std::thread io_client_socket_t1([&io_context_client_socket]() { io_context_client_socket.run(); });
	std::thread io_client_socket_t2([&io_context_client_socket]() { io_context_client_socket.run(); });

	std::thread io_client_read_queue([&io_context2]() { io_context2.run(); });

	

	auto msg = CreateHandshake1Message();

	client.write(std::move(msg));

	client.getNextPayload(msg);
	
	IncomingNetworkMessage msg_(std::move(msg));

	if (msg_.getMessageType() == MessageType::Handshake2)
	{
		client.write(std::move(CreateHandshake3Message()));
		std::cout << "Client Handshake completed\n";
	}
	else {
		std::cout << "Incorrect message received from server\n";
	}


	//client.getNextPayload(msg);

	client.close();
	work.reset();
	work2.reset();

	io_client_socket_t1.join();
	io_client_socket_t2.join();
	io_client_read_queue.join();

	std::cout << "Client threads joined\n";

	return 0;
}

int main()
{
	std::thread server_thread(server);
	std::thread client_thread(client);

	server_thread.join();
	client_thread.join();

	return 0;
}