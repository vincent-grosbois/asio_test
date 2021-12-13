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
#include <cassert>


namespace NetworkMessages {

	// represent raw low-level data sent accross sockets
	typedef std::vector<uint8_t> NetworkMessage;

	// make a NetworkMessage from payload (the actual message we are interested to send)
	NetworkMessage MakeMessage(const std::vector<uint8_t>& payload)
	{
		// header + size + payload + footer
		const uint32_t totalSize = 2 + 4 + payload.size() + 2;

		std::vector<uint8_t> message(totalSize);

		// header magic values
		message[0] = 0xFE;
		message[1] = 0xEF;

		// encode the size of the message
		message[2 + 0] = (totalSize & 0xFF);
		message[2 + 1] = ((totalSize >> 8) & 0xFF);
		message[2 + 2] = ((totalSize >> 16) & 0xFF);
		message[2 + 3] = ((totalSize >> 24) & 0xFF);

		// copy payload starting at byte 6
		std::copy(payload.begin(), payload.end(), message.begin() + 6);

		// footer magic values
		message[message.size() - 2] = 0xFA;
		message[message.size() - 1] = 0xAF;

		return message;
	}

	// checks if a NetworkMessage is correctly formed
	bool IsNetworkMessageValid(const NetworkMessage& message)
	{
		return true; // TODO
	}


}


using namespace NetworkMessages;


// Helper class to read a NetworkMessage from socket.
// The difficulty lies in the fact that reading from socket may retrieve an incomplete NetworkMessage,
// or several NeworkMessages who last message is incomplete.
class SocketReader
{
	tcp::socket& _socket;

	std::vector<uint8_t> _socketBuffer; // buffer of data extracted from socket
	size_t _bytesAvailableInBuffer = 0; // number of bytes in the buffer that actually contain socket data
	int _bufferCurrentPos; // where to read when we continue reading the buffer

	bool _shouldReadFromSocket; // should the next read be from the socket or from the buffer

	static const int _bufferSize = 128; // max size of the buffer to read data from socket
	static_assert(_bufferSize > 6, "BufferSize > 6");

public:
	SocketReader(tcp::socket& socket) :
		_bufferCurrentPos(0),
		_socket(socket),
		_shouldReadFromSocket(true)
	{
		_socketBuffer.resize(_bufferSize);
	}

	// Blocks until one full NetworkMessage is read, or the connection is reset
	bool ReadNextNetworkMessage(NetworkMessage& message)
	{
		uint32_t messageSize; // size of the message : 8 + payload
		uint32_t payloadSize; // size of the relevant data

		NetworkMessage payload; // current payload being written
		uint32_t currentPayloadPos; // current location of where the payload is being written

		bool shouldReadHeader = true; // next read will have to parse the header

		while (true)
		{
			asio::error_code error;

			if (_shouldReadFromSocket) {
				_bytesAvailableInBuffer = _socket.read_some(asio::buffer(_socketBuffer), error);
				_bufferCurrentPos = 0;
			}
			else if (shouldReadHeader && _bytesAvailableInBuffer - _bufferCurrentPos < 6)
			{
				// we still have data in our buffer but not enough to build the header
				// in this case we pull from the socket enough data to have at least the rest of the header first

				// remaining bytes in the buffer, so < 6
				const int M = _bytesAvailableInBuffer - _bufferCurrentPos;

				// copy them at the start of a new buffer
				std::vector<uint8_t> newBuffer(_bufferSize);
				std::copy(_socketBuffer.begin() + _bufferCurrentPos, _socketBuffer.begin() + _bufferCurrentPos + M, newBuffer.begin());

				// remainder of _bufferSize bytes are pulled from socket
				std::vector<uint8_t> socketBuffer2(_bufferSize - M);
				int bufferSize2 = _socket.read_some(asio::buffer(socketBuffer2), error);

				// fill the new buffer with socket data
				std::copy(socketBuffer2.begin(), socketBuffer2.begin() + bufferSize2, newBuffer.begin() + M);
				_bytesAvailableInBuffer = bufferSize2 + M;
				_socketBuffer = std::move(newBuffer);
				_bufferCurrentPos = 0;
			}

			if (error == asio::error::eof)
			{
				return false; // connection reset cleanly by peer
			}
			else if (error)
			{
				throw asio::system_error(error); // TODO
			}


			if (shouldReadHeader) // read the message header
			{
				if (_bytesAvailableInBuffer - _bufferCurrentPos < 6) { // not enough data in buffer to read the header
					assert(0); //TODO
				}

				// check header magic values
				if (_socketBuffer[_bufferCurrentPos] != 0xFE || _socketBuffer[_bufferCurrentPos + 1] != 0xEF)
				{
					assert(0); //TODO handle
				}

				// read message size info
				messageSize = _socketBuffer[_bufferCurrentPos + 2 + 0];
				messageSize |= _socketBuffer[_bufferCurrentPos + 2 + 1] << 8;
				messageSize |= _socketBuffer[_bufferCurrentPos + 2 + 2] << 16;
				messageSize |= _socketBuffer[_bufferCurrentPos + 2 + 3] << 24;

				payloadSize = messageSize - 8;
				payload.resize(payloadSize, -1);

				currentPayloadPos = 0;
				_bufferCurrentPos += 6;

				shouldReadHeader = false;
			}

			// copy buffer data into payload
			int nbBytesToCopy = std::min(_bytesAvailableInBuffer - _bufferCurrentPos, payloadSize - currentPayloadPos);
			std::copy(
				_socketBuffer.begin() + _bufferCurrentPos,
				_socketBuffer.begin() + _bufferCurrentPos + nbBytesToCopy,
				payload.begin() + currentPayloadPos);

			currentPayloadPos += nbBytesToCopy;
			_bufferCurrentPos += nbBytesToCopy;

			// if we reached end of the payload, check footer magic values
			if (currentPayloadPos == payloadSize && _bufferCurrentPos < _bytesAvailableInBuffer)
			{
				if (_socketBuffer[_bufferCurrentPos] != 0xFA)
				{
					assert(0); // TODO handle
				}
				++currentPayloadPos;
				++_bufferCurrentPos;
			}

			if (currentPayloadPos == payloadSize + 1 && _bufferCurrentPos < _bytesAvailableInBuffer)
			{
				if (_socketBuffer[_bufferCurrentPos] != 0xAF)
				{
					assert(0); // TODO handle
				}
				++currentPayloadPos;
				++_bufferCurrentPos;
			}

			if (currentPayloadPos == payloadSize + 2) // we finished reading message up to the end
			{
				if (_bufferCurrentPos == _bytesAvailableInBuffer)
				{
					// exhausted current socket data : next read has to read from socket again
					_shouldReadFromSocket = true;
				}
				else
				{
					// we still have data in the buffer : next read has to read what's left in the buffer
					_shouldReadFromSocket = false;
				}
				message = std::move(payload);
				std::cout << "buffer pos is " << _bufferCurrentPos << ", should read from socket " << _shouldReadFromSocket  << std::endl;
				return true; 
			}
			else {
				// exhausted current socket data : next read has to read from socket again
				_shouldReadFromSocket = true;
			}
		}

	}
};

// Manages a connection to an endpoint, and allows read / write of Network Messages in queues
class ConnectionManager
{

public:
	ConnectionManager(const std::string& name) :
		_name(name),
		_isConnected(false),
		_isServer(false),
		_socket(_io_context),
		_socketReader(_socket)
	{

	}

	bool connectToServer(const std::string& ip, const std::string& port)
	{
		std::lock_guard<std::mutex> guard(_socketLock);
		if (_isConnected) {
			return false;
		}

		tcp::resolver resolver(_io_context);
		tcp::resolver::results_type endpoints = resolver.resolve(ip, port);

		asio::connect(_socket, endpoints);
		_isConnected = true;
		_isServer = false;
		return true;
	}

	bool waitForClient(const int port)
	{
		std::lock_guard<std::mutex> guard(_socketLock);
		if (_isConnected) {
			return false;
		}

		tcp::acceptor acceptor(_io_context, tcp::endpoint(tcp::v4(), port));
		acceptor.accept(_socket);

		_isConnected = true;
		_isServer = true;
		return true;
	}

	bool hasIncomingMessage() const
	{
		std::lock_guard<std::mutex> guard(_incomingMessagesToReadLock);
		return !_incomingMessagesToRead.empty();
	}

	bool popIncomingMessage(NetworkMessage& message)
	{
		std::lock_guard<std::mutex> guard(_incomingMessagesToReadLock);
		if (_incomingMessagesToRead.empty())
		{
			return false;
		}

		message = _incomingMessagesToRead[0];
		_incomingMessagesToRead.pop_front();
		return true;
	}

	void clearIncomingMessages()
	{
		std::lock_guard<std::mutex> guard(_incomingMessagesToReadLock);
		_incomingMessagesToRead.clear();
	}

	void postOutgoingMessage(NetworkMessage& message)
	{
		NetworkMessage m = message;
		std::lock_guard<std::mutex> guard(_outgoingMessagesToWriteLock);
		_outgoingMessagesToWrite.push_back(std::move(m));
	}

	void writeOutgoingMessageToSocket()
	{
		std::lock_guard<std::mutex> guard(_outgoingMessagesToWriteLock);

		if (_outgoingMessagesToWrite.empty())
		{
			return;
		}

		std::vector<uint8_t> m = _outgoingMessagesToWrite[0];
		_outgoingMessagesToWrite.pop_front();

		std::lock_guard<std::mutex> guard2(_socketLock);
		asio::error_code ignored_error;

		asio::write(_socket, asio::buffer(m), ignored_error);

		//std::cout << _name << ": wrote " << m.size() << " byte" << std::endl;
	}

	void waitForIncomingMessageFromSocket()
	{
		std::lock_guard<std::mutex> socketGuard(_socketLock);

		NetworkMessage msg;
		bool isConnectionOk = _socketReader.ReadNextNetworkMessage(msg);

		if (!isConnectionOk)
		{
			std::cout << _name << ": connection closed" << std::endl;
			return;
		}

		std::cout << _name << ": received message with payload " << msg.size() << " bytes" << std::endl;

		std::lock_guard<std::mutex> guard(_incomingMessagesToReadLock);
		_incomingMessagesToRead.emplace_back(std::move(msg));
	}


private:

	// for debug only
	const std::string _name;

	asio::io_context _io_context;

	// the socket for this connection
	tcp::socket _socket;
	std::mutex _socketLock;

	// queue of incoming messages
	std::deque<NetworkMessage> _incomingMessagesToRead;
	mutable std::mutex _incomingMessagesToReadLock;

	// queue of outgoing messages
	std::deque<NetworkMessage> _outgoingMessagesToWrite;
	std::mutex _outgoingMessagesToWriteLock;

	// Helper class to read data from socket
	SocketReader _socketReader;

	bool _isConnected;
	bool _isServer;
};
