#ifndef MESSAGES_H
#define MESSAGES_H 1

#include "Kmer.h"
#include "KmerData.h"
#include "NetworkDefs.h"
#include <ostream>

class NetworkSequenceCollection;

enum MessageType
{
	MT_VOID,
	MT_SEQ_OP,
	MT_SET_FLAG,
	MT_REMOVE_EXT,
	MT_SEQ_DATA_REQUEST,
	MT_SEQ_DATA_RESPONSE,
	MT_SET_BASE
};

enum MessageOp
{
	MO_VOID,
	MO_ADD,
	MO_REMOVE,
};

typedef uint32_t IDType;

class Message
{
	public:
		Message() { }
		Message(const Kmer& seq) : m_seq(seq) { }
		virtual ~Message() { }

		virtual void handle(int senderID, NetworkSequenceCollection& handler) = 0;

		virtual size_t getNetworkSize() const
		{
			return sizeof (uint8_t) // MessageType
				+ Kmer::serialSize();
		}

		static MessageType readMessageType(char* buffer);
		virtual size_t serialize(char* buffer) = 0;
		virtual size_t unserialize(const char* buffer);
		virtual void print() const { }

		friend std::ostream& operator <<(std::ostream& o,
				const Message& m)
		{
			m.print();
			return o;
		}

		Kmer m_seq;
};

class SeqOpMessage : public Message
{
	public:
		SeqOpMessage() { }
		SeqOpMessage(const Kmer& seq, MessageOp operation)
			: Message(seq), m_operation(operation) { }

		size_t getNetworkSize() const
		{
			return Message::getNetworkSize() + sizeof m_operation;
		}

		void handle(int senderID, NetworkSequenceCollection& handler);
		size_t serialize(char* buffer);
		size_t unserialize(const char* buffer);
		void print() const;

		static const MessageType TYPE = MT_SEQ_OP;
		uint8_t m_operation; // MessageOp
};

class SetFlagMessage : public Message
{
	public:
		SetFlagMessage() { }
		SetFlagMessage(const Kmer& seq, SeqFlag flag)
			: Message(seq), m_flag(flag) { }

		size_t getNetworkSize() const
		{
			return Message::getNetworkSize() + sizeof m_flag;
		}

		void handle(int senderID, NetworkSequenceCollection& handler);
		size_t serialize(char* buffer);
		size_t unserialize(const char* buffer);
		void print() const;

		static const MessageType TYPE = MT_SET_FLAG;
		uint8_t m_flag; // SeqFlag
};

class RemoveExtensionMessage : public Message
{
	public:
		RemoveExtensionMessage() { }
		RemoveExtensionMessage(const Kmer& seq,
				extDirection dir, SeqExt ext)
			: Message(seq), m_dir(dir), m_ext(ext) { }

		size_t getNetworkSize() const
		{
			return Message::getNetworkSize()
				+ sizeof m_dir + sizeof m_ext;
		}

		void handle(int senderID, NetworkSequenceCollection& handler);
		size_t serialize(char* buffer);
		size_t unserialize(const char* buffer);
		void print() const;

		static const MessageType TYPE = MT_REMOVE_EXT;
		uint8_t m_dir; // extDirection
		SeqExt m_ext;
};

class SeqDataRequest : public Message
{
	public:
		SeqDataRequest() { }
		SeqDataRequest(const Kmer& seq, IDType group, IDType id)
			: Message(seq), m_group(group), m_id(id) { }

		size_t getNetworkSize() const
		{
			return Message::getNetworkSize()
				+ sizeof m_group + sizeof m_id;
		}

		void handle(int senderID, NetworkSequenceCollection& handler);
		size_t serialize(char* buffer);
		size_t unserialize(const char* buffer);
		void print() const;

		static const MessageType TYPE = MT_SEQ_DATA_REQUEST;
		IDType m_group;
		IDType m_id;
};

class SeqDataResponse : public Message
{
	public:
		SeqDataResponse() { }
		SeqDataResponse(const Kmer& seq, IDType group, IDType id,
				ExtensionRecord& extRecord, int multiplicity) :
			Message(seq), m_group(group), m_id(id),
			m_extRecord(extRecord), m_multiplicity(multiplicity) { }

		size_t getNetworkSize() const
		{
			return Message::getNetworkSize()
				+ sizeof m_group + sizeof m_id
				+ sizeof m_extRecord + sizeof m_multiplicity;
		}

		void handle(int senderID, NetworkSequenceCollection& handler);
		size_t serialize(char* buffer);
		size_t unserialize(const char* buffer);
		void print() const;

		static const MessageType TYPE = MT_SEQ_DATA_RESPONSE;
		IDType m_group;
		IDType m_id;
		ExtensionRecord m_extRecord;
		uint16_t m_multiplicity;
};

class SetBaseMessage : public Message
{
	public:
		SetBaseMessage() { }
		SetBaseMessage(const Kmer& seq,
				extDirection dir, uint8_t base)
			: Message(seq), m_dir(dir), m_base(base) { }

		size_t getNetworkSize() const
		{
			return Message::getNetworkSize()
				+ sizeof m_dir + sizeof m_base;
		}

		void handle(int senderID, NetworkSequenceCollection& handler);
		size_t serialize(char* buffer);
		size_t unserialize(const char* buffer);
		void print() const;

		static const MessageType TYPE = MT_SET_BASE;
		uint8_t m_dir; // extDirection
		uint8_t m_base;
};

#endif
