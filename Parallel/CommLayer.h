#ifndef COMMLAYER
#define COMMLAYER

#include <mpi.h>
#include <list>
#include "PackedSeq.h"
#include "NetworkDefs.h"


struct SeqMessage
{
	APSeqOperation operation;
	PackedSeq seq;
};

struct SeqFlagMessage
{
	APSeqFlagOperation operation;
	PackedSeq seq;
	SeqFlag flag;
};

struct SeqExtMessage
{
	APSeqExtOperation operation;
	PackedSeq seq;
	extDirection dir;	
	SeqExt ext;
	char base;
	
};

struct ControlMessage
{
	APControl msgType;
	int argument;
};

struct ResultMessage
{
	APResult result[2];
};

struct RequestBuffer
{
	char* buffer;
	MPI::Request request;
};
const int CONTROL_ID = 0;

typedef std::list<MPI::Request> RequestList;
// The comm layer wraps inter-process communication operations
class CommLayer
{
	public:
	
		// Constructor/Destructor
		CommLayer(int id, int kmerSize);
		~CommLayer();
	
		// Check if a message exists, if it does return the type
		APMessage CheckMessage(int &sendID) const;
		
		// Send a control message
		void SendControlMessage(int numNodes, APControl m, int argument = 0) const;
		
		// Send a control message to a specific node
		void SendControlMessageToNode(int nodeID, APControl m, int argument = 0) const;
		
		// Send a message that the checkpoint has been reached
		void SendCheckPointMessage(int argument = 0) const;
		
		// Send a sequence to a specific id
		void SendSeqMessage(int destID, const PackedSeq& seq, APSeqOperation operation) const;
		
		// Send a sequence extension message
		void SendSeqExtMessage(int destID, const PackedSeq& seq, APSeqExtOperation operation, extDirection dir, SeqExt ext, char base = 0) const;
		
		// Send a sequence flag message
		void SendSeqFlagMessage(int destID, const PackedSeq& seq, APSeqFlagOperation operation, SeqFlag flag) const;
		
		// Send a bool result
		void SendResultMessage(int destID, bool b);
		
		// Send a result
		void SendResultMessage(int destID, ResultPair rp);
		
		// Receive a seq message
		SeqMessage ReceiveSeqMessage();
		
		// Receive a seq message
		SeqExtMessage ReceiveSeqExtMessage();
		
		// Receive a seq message
		SeqFlagMessage ReceiveSeqFlagMessage();		
			
		// Receive a control message
		ControlMessage ReceiveControlMessage();
		
		// Receive a result message
		ResultMessage ReceiveResultMessage();
		
		// Flush the buffer
		void flush();
		
	private:
		int m_id;
		int m_kmerSize;
		int m_numBytesPerSeq;
		int m_bufferSize;
		char* m_buffer;
		mutable unsigned long m_numSends;
};

#endif
