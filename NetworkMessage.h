#pragma once

#include <cstdint>
#include <vector>

enum class MessageType : uint16_t
{
	Unknown,
	Handshake1,
	Handshake2,
	Handshake3,
	LastMessageType,
};

class IncomingNetworkMessage
{

public:
	IncomingNetworkMessage(std::vector<uint8_t>&& payload) :
		_data(std::move(payload))
	{
		if (_data.size() < 2)
		{
			_messageType = MessageType::Unknown;
		}
		else {
			uint16_t messageTypeInt = (static_cast<uint16_t>(_data[0]) << 8) | _data[1];
			if (messageTypeInt >= static_cast<uint16_t>(MessageType::LastMessageType))
			{
				messageTypeInt = static_cast<uint16_t>(MessageType::Unknown);
			}
			_messageType = static_cast<MessageType>(messageTypeInt);
		}

	}

	MessageType getMessageType() const {
		return _messageType;
	}

private:
	MessageType _messageType;
	std::vector<uint8_t> _data;
};

typedef std::vector<uint8_t> OutgoingNetworkMessage;

OutgoingNetworkMessage CreateMessage(const MessageType message)
{
	const uint16_t messageTypeInt = static_cast<uint16_t>(message);

	OutgoingNetworkMessage result(2);

	result[0] = (messageTypeInt << 8) & 0xFF;
	result[1] = messageTypeInt & 0xFF;
	return result;
}

OutgoingNetworkMessage CreateHandshake1Message()
{
	return CreateMessage(MessageType::Handshake1);
}

OutgoingNetworkMessage CreateHandshake2Message()
{
	return CreateMessage(MessageType::Handshake2);
}

OutgoingNetworkMessage CreateHandshake3Message()
{
	return CreateMessage(MessageType::Handshake3);
}