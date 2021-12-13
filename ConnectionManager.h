#include <atomic>
#include <iostream>
#include <string>
#include <cstdint>
#include <chrono>
#include <vector>
#include <mutex>
#include <deque>
#include <cassert>

#include "include/asio.hpp"
using asio::ip::tcp;

namespace NetworkMessages {
	// represent the data we want to send accross the socket
	typedef std::vector<uint8_t> Payload;
}


// Helper class to read a NetworkMessage from socket.
// The difficulty lies in the fact that reading from socket may retrieve an incomplete NetworkMessage,
// or several NeworkMessages who last message is incomplete.
class SocketReader
{

public:
	SocketReader(tcp::socket& socket);

	// Blocks until one full Payload is read, or the connection is reset
	NetworkMessages::Payload ReadNextNetworkPayload();

private:
	tcp::socket& _socket;

	std::vector<uint8_t> _socketBuffer; // buffer of data extracted from socket
	size_t _bytesAvailableInBuffer = 0; // number of bytes in the buffer that actually contain socket data
	size_t _bufferCurrentPos; // where to read when we continue reading the buffer

	bool _shouldReadFromSocket; // should the next read be from the socket or from the buffer

	static const int _bufferSize = 1000; // max size of the buffer to read data from socket
	static_assert(_bufferSize > 6, "BufferSize > 6");
};

// Manages a connection to an endpoint, and allows read / write of Network Messages in queues
class ConnectionManager
{

public:
	ConnectionManager(const std::string& name);

	std::string getName() const;

	bool connectToServer(const std::string& ip, const std::string& port);
	bool waitForClient(const int port); // blocks until a client joins in
	bool isConnected() const;

	bool writeAllAndStop();
	bool isStopped() const;

	bool popIncomingPayload(NetworkMessages::Payload& message);
	bool waitForIncomingMessageFromSocket();

	void pushOutgoingPayload(const NetworkMessages::Payload& payload);
	bool writeOutgoingMessageToSocket();

private:
	// represent raw low-level data sent accross sockets, including header and footer
	typedef std::vector<uint8_t> NetworkMessage;

	// for debug only
	const std::string _name;

	asio::io_context _io_context;

	// the socket for this connection
	tcp::socket _socket;
	std::recursive_mutex _socketLock;

	// queue of incoming payloads that were received from socket
	std::deque<NetworkMessages::Payload> _incomingPayloadsToRead;
	mutable std::recursive_mutex _incomingPayloadsToReadLock;

	// queue of outgoing messages that were not sent to socket yet
	std::deque<NetworkMessage> _outgoingMessagesToWrite;
	std::recursive_mutex _outgoingMessagesToWriteLock;

	// Helper class to read data from socket
	SocketReader _socketReader;

	std::atomic<bool> _isConnected;
	std::atomic<bool> _isStopped;

	static NetworkMessage MakeMessage(const NetworkMessages::Payload& payload);
};
