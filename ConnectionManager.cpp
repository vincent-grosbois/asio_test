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
#include "ConnectionManager.h"


using namespace NetworkMessages;
using asio::ip::tcp;


// Helper class to read a NetworkMessage from socket.
// The difficulty lies in the fact that reading from socket may retrieve an incomplete NetworkMessage,
// or several NeworkMessages who last message is incomplete.
SocketReader::SocketReader(tcp::socket& socket) :
	_bufferCurrentPos(0),
	_socket(socket),
	_shouldReadFromSocket(true)
{
	_socketBuffer.resize(_bufferSize);
}

// Blocks until one full NetworkPayload is read, or the connection is reset
Payload SocketReader::ReadNextNetworkPayload()
{
	uint32_t currentPayloadPos = 0; // current location of where the payload is being copied

	uint32_t messageSize = -1; // size of the message : 8 + payload
	uint32_t payloadSize = -1; // size of the relevant data

	bool shouldReadHeader = true; // next read will have to parse the header

	Payload payload;

	while (true)
	{

		if (_shouldReadFromSocket) {
			_bufferCurrentPos = 0;
			_bytesAvailableInBuffer = _socket.read_some(asio::buffer(_socketBuffer));
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
			int bufferSize2 = _socket.read_some(asio::buffer(socketBuffer2));

			// fill the new buffer with socket data
			std::copy(socketBuffer2.begin(), socketBuffer2.begin() + bufferSize2, newBuffer.begin() + M);
			_bytesAvailableInBuffer = bufferSize2 + M;
			_socketBuffer = std::move(newBuffer);
			_bufferCurrentPos = 0;
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

		if (currentPayloadPos < payloadSize) {
			// copy buffer data into payload
			int nbBytesToCopy = std::min(_bytesAvailableInBuffer - _bufferCurrentPos, payloadSize - currentPayloadPos);
			std::copy(
				_socketBuffer.begin() + _bufferCurrentPos,
				_socketBuffer.begin() + _bufferCurrentPos + nbBytesToCopy,
				payload.begin() + currentPayloadPos);

			currentPayloadPos += nbBytesToCopy;
			_bufferCurrentPos += nbBytesToCopy;
		}

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

		// we copied the payload up to the end
		if (currentPayloadPos == payloadSize + 2)
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
			return payload;
		}
		else {
			// exhausted current socket data : next read has to read from socket again
			_shouldReadFromSocket = true;
		}
	}

}

ConnectionManager::ConnectionManager(const std::string& name) :
	_name(name),
	_isConnected(false),
	_isStopped(false),
	_socket(_io_context),
	_socketReader(_socket)
{

}

std::string ConnectionManager::getName() const
{
	return _name;
}

bool ConnectionManager::isConnected() const
{
	return _isConnected;
}

bool ConnectionManager::isStopped() const
{
	return _isStopped;
}

bool ConnectionManager::writeAllAndStop()
{
	if (_isStopped || !_isConnected)
	{
		return false;
	}

	std::lock_guard<std::recursive_mutex> guard(_socketLock);
	if (_isStopped)
	{
		return false;
	}

	std::lock_guard<std::recursive_mutex> writeLock(_outgoingMessagesToWriteLock);

	// try to write everything to socket, but be careful that writeOutgoingMessageToSocket may also stop the socket if it fails
	while (!_outgoingMessagesToWrite.empty() && _isConnected && !_isStopped)
	{
		writeOutgoingMessageToSocket();
	}


	_socket.close();
	_isStopped = true;
	_isConnected = false;
	return true;
}



bool ConnectionManager::connectToServer(const std::string& ip, const std::string& port)
{
	std::lock_guard<std::recursive_mutex> guard(_socketLock);

	if (_isConnected || _isStopped) {
		// we don't allow reconnection with same ConnectionManager instance
		return false;
	}

	tcp::resolver resolver(_io_context);
	tcp::resolver::results_type endpoints = resolver.resolve(ip, port);
	asio::connect(_socket, endpoints);
	_socket.set_option(asio::detail::socket_option::integer<SOL_SOCKET, SO_RCVTIMEO>{ 5 });

	_isConnected = true;
	_isStopped = false;
	return true;
}


// blocks until a client joins in
bool ConnectionManager::waitForClient(const int port)
{
	std::lock_guard<std::recursive_mutex> guard(_socketLock);

	if (_isConnected || _isStopped) {
		// we don't allow reconnection with same ConnectionManager instance
		return false;
	}

	tcp::acceptor acceptor(_io_context, tcp::endpoint(tcp::v4(), port));
	acceptor.accept(_socket);
	_socket.set_option(asio::detail::socket_option::integer<SOL_SOCKET, SO_RCVTIMEO>{ 5 });

	_isConnected = true;
	_isStopped = false;
	return true;
}

bool ConnectionManager::popIncomingPayload(Payload& message)
{
	std::lock_guard<std::recursive_mutex> guard(_incomingPayloadsToReadLock);
	if (_incomingPayloadsToRead.empty())
	{
		return false;
	}

	message = std::move(_incomingPayloadsToRead[0]);
	_incomingPayloadsToRead.pop_front();
	return true;
}

void ConnectionManager::pushOutgoingPayload(const Payload& payload)
{
	NetworkMessage message = MakeMessage(payload);
	std::lock_guard<std::recursive_mutex> guard(_outgoingMessagesToWriteLock);
	_outgoingMessagesToWrite.emplace_back(std::move(message));
}

bool ConnectionManager::writeOutgoingMessageToSocket()
{
	if (!_isConnected || _isStopped)
	{
		return false;
	}

	std::lock_guard<std::recursive_mutex> queueGuard(_outgoingMessagesToWriteLock);

	if (_outgoingMessagesToWrite.empty())
	{
		return false;
	}

	std::lock_guard<std::recursive_mutex> socketGuard(_socketLock);
	if (_isStopped)
	{
		return false;
	}

	const size_t messageSize = _outgoingMessagesToWrite[0].size();

	try {
		asio::write(_socket, asio::buffer(_outgoingMessagesToWrite[0]));
		_outgoingMessagesToWrite.pop_front();
	}
	catch (asio::system_error error)
	{
		if (error.code() == asio::error::eof)
		{
			_isConnected = false;
			_isStopped = true;
			return false;
		}
		else
		{
			// other errors are unexpected: rethrow
			throw error;
		}
	}

	std::cout << _name << ": sent " << messageSize << " bytes over socket" << std::endl;

	return true;
}

bool ConnectionManager::waitForIncomingMessageFromSocket()
{
	if (!_isConnected || _isStopped)
	{
		return false;
	}

	Payload payload;

	{
		std::lock_guard<std::recursive_mutex> socketGuard(_socketLock);
		if (_isStopped)
		{
			return false;
		}
		try {
			payload = _socketReader.ReadNextNetworkPayload();
		}
		catch (asio::system_error error)
		{
			if (error.code() == asio::error::eof)
			{
				_isConnected = false;
				_isStopped = true;
				//std::cout << _name << ": deconnected" << std::endl;
				return false; // reach end of file of socket : deconnected
			}
			if (error.code() == asio::error::timed_out)
			{
				return false; // read timed out : probably nothing to read
			}
			else
			{
				// other errors are unexpected: rethrow
				throw error;
			}
		}
	}

	std::cout << _name << ": received message with payload " << payload.size() << " bytes" << std::endl;

	std::lock_guard<std::recursive_mutex> guard(_incomingPayloadsToReadLock);
	_incomingPayloadsToRead.emplace_back(std::move(payload));
	return true;
}


ConnectionManager::NetworkMessage ConnectionManager::MakeMessage(const Payload& payload)
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
