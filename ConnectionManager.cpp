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


namespace {

	std::array<uint8_t, 6> MakeHeader(const std::vector<uint8_t>& payload)
	{
		// header magic values
		std::array<uint8_t, 6> header = { 0xFE, 0xEF }; 

		// encode the size of the message
		const uint32_t totalSize = 2 + 4 + payload.size() + 2; // header + size + payload + footer
		header[2] = (totalSize & 0xFF);
		header[3] = ((totalSize >> 8) & 0xFF);
		header[4] = ((totalSize >> 16) & 0xFF);
		header[5] = ((totalSize >> 24) & 0xFF);

		return header;
	}

}


PayloadQueue::PayloadQueue(std::string name, asio::io_context& io_context)
	:
	_name(name),
	_io_context(io_context),
	_queueStrand(io_context) // enforces non-concurrent access to _queue
{ 
}


// tries once to get a payload if one is available
bool PayloadQueue::getPayloadIfAny(std::vector<uint8_t>& payload)
{
	std::atomic<int> signal = -1;

	_queueStrand.post([this, &signal, &payload]() {
		if (!_queue.empty())
		{
			payload = std::move(_queue.front());
			_queue.pop_front();
			signal = 1;
		}
		else {
			signal = 0;
		}
		}
	);

	while (signal == -1) {}
	return signal == 1;
}

// tries to get a payload by blocking, otherwise abort after the timeout is elapsed
bool PayloadQueue::getNextPayload(std::vector<uint8_t>& payload, int timeoutMilliseconds)
{
	std::atomic<int> signal = -1;
	const std::chrono::steady_clock::time_point now = std::chrono::high_resolution_clock::now();
	const std::chrono::steady_clock::time_point expirationTime = now + std::chrono::milliseconds(timeoutMilliseconds);

	_queueStrand.post([this, &signal, &payload, &expirationTime]() {
		tryGetPayload(signal, payload, expirationTime);
		}
	);

	while (signal == -1) {}
	if (signal == 1)
	{
		std::cout << _name << "_payloadQueue : extracted message of size " << payload.size() + 8 << '\n';
	}
	else {
		std::cout << _name << "_payloadQueue : extracting message timed out\n";
	}

	return signal == 1;
}

void PayloadQueue::tryGetPayload(std::atomic<int>& signal, std::vector<uint8_t>& payload, std::chrono::steady_clock::time_point expirationTime)
{
	if (!_queue.empty())
	{
		payload = std::move(_queue.front());
		_queue.pop_front();
		signal = 1; // signal that we found some value
	}
	else {
		const std::chrono::steady_clock::time_point now = std::chrono::high_resolution_clock::now();
		if (now < expirationTime)
		{
			// Post a task that will retry. Potentially another task is already pending that will add an item.
			_queueStrand.post([this, &signal, &payload, expirationTime]() { tryGetPayload(signal, payload, expirationTime); });
		}
		else
		{
			signal = 0; // no time left, signal that we abort
		}
	}
}


void PayloadQueue::addPayload(std::vector<uint8_t>&& payload)
{
	_queueStrand.post([this, payload_ = std::move(payload)]() mutable {
		// crop the footer
		payload_.resize(payload_.size() - 2);
		std::cout << _name << "_payloadQueue : pushed to queue message of size " << payload_.size() + 8 << '\n';
		_queue.emplace_back(std::move(payload_));
	});
}


NetworkConnection::NetworkConnection(
	const std::string& name,
	asio::io_context& io_context_for_socket,
	asio::io_context& io_context_for_payload_queue)
	:
	_name(name),
	_ioContext(io_context_for_socket),
	_writeStrand(io_context_for_socket),
	_socket(io_context_for_socket),
	_payloadQueue(name, io_context_for_payload_queue),
	_readMessageHeader(),
	_isConnected(false),
	_isWriting(false)
{
}

bool NetworkConnection::connectSync(const asio::ip::tcp::endpoint endpoint)
{
	if (_isConnected)
	{
		return false;
	}

	asio::error_code ec;
	_socket.connect(endpoint, ec);

	if (ec)
	{
		std::cout << _name << " : Connection failed with error " << ec.message() << "\n";
		return false;
	}

	_isConnected = true;
	_ioContext.post([this]() { do_read_header(); });

	return true;
}

bool NetworkConnection::acceptConnectionSync(const int port)
{
	if (_isConnected)
	{
		return false;
	}

	asio::ip::tcp::acceptor acceptor(_ioContext, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port));
	asio::error_code ec;
	acceptor.accept(_socket, ec);

	if (ec)
	{
		std::cout << _name << " : Acceptor failed with error " << ec.message() << "\n";
		return false;
	}

	_isConnected = true;
	_ioContext.post([this]() { do_read_header(); });

	return true;
}

void NetworkConnection::close()
{
	asio::post(_ioContext, [this]() {
		_socket.close();
		_isConnected = false;
		});
}

void NetworkConnection::closeAfterAllPendingWrites()
{
	asio::post(_writeStrand, [this]() {

		if (_isConnected && (!_messagesToWrite.empty() || _isWriting))
		{
			closeAfterAllPendingWrites();
		}
		else {
			_socket.close();
			_isConnected = false;
		}
		});
}


// asynchronously sends a payload to the endpoint. This is thread-safe.
bool NetworkConnection::write(std::vector<uint8_t>&& payload) {

	if (!_isConnected)
	{
		return false;
	}

	_writeStrand.post(
		[this, payload_ = std::move(payload)]() mutable {
		MessageData messageData(MakeHeader(payload_), std::move(payload_));
		_messagesToWrite.emplace_back(std::move(messageData));
		do_write();
	});

	return true;
}

bool NetworkConnection::getPayloadIfAny(std::vector<uint8_t>& payload)
{
	return _payloadQueue.getPayloadIfAny(payload);
}

bool NetworkConnection::getNextPayload(std::vector<uint8_t>& payload, int timeoutMilliseconds /*= 10*/)
{
	return _payloadQueue.getNextPayload(payload, timeoutMilliseconds);
}


void NetworkConnection::do_read_header()
{
	asio::async_read(_socket,
		asio::buffer(_readMessageHeader),
		[this](asio::error_code ec, std::size_t length)
		{
			if (!ec)
			{
				if (_readMessageHeader[0] != 0xFE || _readMessageHeader[1] != 0xEF) // check header magic values
				{
					std::cout << _name << " : Received invalid header, disconnecting\n";
					close();
				}

				// read message size info
				int messageSize = _readMessageHeader[2];
				messageSize |= _readMessageHeader[3] << 8;
				messageSize |= _readMessageHeader[4] << 16;
				messageSize |= _readMessageHeader[5] << 24;

				const int payloadSize = messageSize - 8;
				do_read_body(payloadSize);
			}
			else
			{
				std::cout << _name << " : Received error code in do_read_header " << ec.message() << "\n";
				close();
			}
		});
}

void NetworkConnection::do_read_body(const int payloadSize)
{
	// resize to have enough data for payload + footer
	_readMessageData.resize(payloadSize + 2);

	asio::async_read(_socket,
		asio::buffer(_readMessageData),
		[this, payloadSize](asio::error_code ec, std::size_t length)
		{
			if (!ec)
			{
				if (_readMessageData[payloadSize] != 0xFA || _readMessageData[payloadSize + 1] != 0xAF) // check footer magic values
				{
					std::cout << _name << " : Received invalid footer, disconnecting\n";
					close();
				}

				std::cout << _name << " : Read message of size " << payloadSize + 8 << "\n";
				_payloadQueue.addPayload(std::move(_readMessageData));
				do_read_header();
			}
			else
			{
				std::cout << _name << " : Received error code in do_read_body " << ec.message() << "\n";
				close();
			}
		});
}

void NetworkConnection::do_write()
{
	if (_messagesToWrite.empty() || _isWriting)
	{
		return;
	}

	_isWriting = true;

	const MessageData& firstMessage = _messagesToWrite.front();

	std::array<asio::const_buffer, 3> sequence{ asio::buffer(firstMessage.first), asio::buffer(firstMessage.second), asio::buffer(_messageFooter) };

	asio::async_write(_socket, sequence, _writeStrand.wrap(
		[this](asio::error_code ec, std::size_t length)
		{
			_isWriting = false;
			if (!ec)
			{
				std::cout << _name << " : Wrote " << length << " bytes \n";
				_messagesToWrite.pop_front();
				do_write();
			}
			else
			{
				std::cout << _name << " : Received error code in async_write " << ec.message() << "\n";
				close();
			}
		}
	));
}
