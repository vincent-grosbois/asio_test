#pragma once

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


class PayloadQueue
{

public:
	PayloadQueue(std::string name,
		asio::io_context& io_context);

	// tries once to get a payload if one is available
	bool getPayloadIfAny(std::vector<uint8_t>& payload);

	// tries to get a payload by blocking, otherwise abort after the timeout is elapsed
	bool getNextPayload(std::vector<uint8_t>& payload, int timeoutMilliseconds);

	void addPayload(std::vector<uint8_t>&& payload);

private:

	void tryGetPayload(std::atomic<int>& signal, std::vector<uint8_t>& payload, std::chrono::steady_clock::time_point expirationTime);

	std::string _name;
	asio::io_context& _io_context;
	asio::io_service::strand _queueStrand;
	std::deque< std::vector<uint8_t> > _queue;
};



class NetworkConnection
{
public:

	NetworkConnection(
		const std::string& name,
		asio::io_context& io_context_for_socket,
		asio::io_context& io_context_for_payload_queue);

	NetworkConnection(const NetworkConnection&) = delete;
	NetworkConnection& operator=(const NetworkConnection&) = delete;

	bool connectSync(const asio::ip::tcp::endpoint endpoint);

	bool acceptConnectionSync(const int port);

	void close();

	// asynchronously sends a payload to the endpoint. This is thread-safe.
	bool write(std::vector<uint8_t>&& payload);

	// block and get next payload if there is one ready
	bool getPayloadIfAny(std::vector<uint8_t>& payload);

	// block and get next payload, or aborts if we exceed the timeout
	bool getNextPayload(std::vector<uint8_t>& payload, int timeoutMilliseconds = 10);

private:
	void do_read_header();
	void do_read_body(const int payloadSize);
	void do_write();

	const std::string _name;

	asio::io_context& _ioContext;
	asio::ip::tcp::socket _socket;

	std::atomic<bool> _isConnected;

	std::array<uint8_t, 6> _readMessageHeader;
	std::vector<uint8_t> _readMessageData;

	asio::io_service::strand _writeStrand;
	bool _isWriting;
	std::deque<std::vector<uint8_t>> _messagesToWrite;

	PayloadQueue _payloadQueue;
};
