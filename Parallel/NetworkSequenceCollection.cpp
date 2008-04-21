#include "NetworkSequenceCollection.h"
#include "Options.h"

//
//
//
NetworkSequenceCollection::NetworkSequenceCollection(int myID, 
											 int numDataNodes, 
											 int kmerSize, 
											 int readLen) : m_id(myID), m_numDataNodes(numDataNodes), 
											 m_state(NAS_LOADING), m_kmer(kmerSize), m_readLen(readLen), 
											 m_trimStep(0), m_numAssembled(0)
{
	// Load the phase space
	m_pLocalSpace = new SequenceCollectionHash();
	
	// Create the comm layer
	m_pComm = new CommLayer(myID, kmerSize);
}

//
//
//
NetworkSequenceCollection::~NetworkSequenceCollection()
{
	// Delete the objects created in the constructor
	delete m_pLocalSpace;
	m_pLocalSpace = 0;
	
	delete m_pComm;
	m_pComm = 0;
}
int adjSet = 0;
//
//
//
void NetworkSequenceCollection::run(int /*readLength*/, int kmerSize)
{
	bool stop = false;
	while(!stop)
	{
		switch(m_state)
		{
			case NAS_LOADING:
			{
				// Spin in the message loop, waiting for sequences
				pumpNetwork();
				break;
			}
			case NAS_FINALIZE:
			{
				finalize();
				SetState(NAS_WAITING);
				m_pComm->SendCheckPointMessage();				
				break;
			}
			case NAS_GEN_ADJ:
			{
				networkGenerateAdjacency(this);
				SetState(NAS_WAITING);
				
				// Tell the control process this checkpoint has been reached
				m_pComm->SendCheckPointMessage();
				//SendControlMessageTo 
				break;
			}
			case NAS_TRIM:
			{
				assert(m_trimStep != 0);
				
				// Perform the trim at the branch length that the control node sent
				int numRemoved = trimSequences(this, m_trimStep);
				SetState(NAS_WAITING);
				
				// Tell the control process this checkpoint has been reached
				m_pComm->SendCheckPointMessage(numRemoved);
				break;
			}
			case NAS_POPBUBBLE:
			{
				popBubbles(this, kmerSize);
				m_pComm->SendCheckPointMessage();
				SetState(NAS_WAITING);
				
				break;	
			}
			case NAS_SPLIT:
			{
				splitAmbiguous(this);	
				m_pComm->SendCheckPointMessage();
				printf("slave finished split\n");
				SetState(NAS_WAITING);
				break;
			}
			case NAS_ASSEMBLE:
			{
				printf("%d: Assembling\n", m_id);
				// The slave node opens the file in append mode
				FastaWriter writer("pcontigs.fa", true);
				assemble(this, m_readLen, m_kmer, &writer);
				SetState(NAS_WAITING);
				
				// Tell the control process this checkpoint has been reached
				m_pComm->SendCheckPointMessage();	
				break;
			}
			case NAS_WAITING:
			{
				pumpNetwork();
				break;
			}
			case NAS_DONE:
			{
				stop = true;
				break;
			}
			assert(false);			
		}
	}
}

//
// The main loop for the controller (rank = 0 process)
//
void NetworkSequenceCollection::runControl(std::string fastaFile, int readLength, int kmerSize)
{
	bool stop = false;
	while(!stop)
	{
		switch(m_state)
		{
			case NAS_LOADING:
			{
				loadSequences(this, fastaFile, readLength, kmerSize);
				SetState(NAS_FINALIZE);
				
				// Tell the rest of the loaders that the load is finished
				m_pComm->SendControlMessage(m_numDataNodes, APC_DONELOAD);
				
				// Touch a file to indicate the load is done
				ofstream dummyFile("load.done");
				dummyFile.close();
				break;
			}
			case NAS_FINALIZE:
			{
				finalize();
				m_numReachedCheckpoint++;
				while(!checkpointReached(m_numDataNodes))
				{
					pumpNetwork();
				}
				
				SetState(NAS_GEN_ADJ);
				m_pComm->SendControlMessage(m_numDataNodes, APC_GEN_ADJ);				
				break;
			}
			case NAS_GEN_ADJ:
			{
  				double starttime, endtime; 
  				starttime = MPI::Wtime();
				networkGenerateAdjacency(this);
				m_numReachedCheckpoint++;
				while(!checkpointReached(m_numDataNodes))
				{
					pumpNetwork();
				}
				endtime = MPI::Wtime();
				printf("Gen adjacency took %f seconds\n", endtime - starttime);
				SetState(NAS_TRIM);
				break;
			}
			case NAS_TRIM:
			{
				// The control node drives the trimming and passes the value to trim at to the other nodes
				int start = 2;
				
				bool stopTrimming = false;
				while(!stopTrimming)
				{
					m_pComm->SendControlMessage(m_numDataNodes, APC_TRIM, start);
					
					// perform the trim
					int numRemoved = trimSequences(this, start);
					
					if (start < opt::trimLen)
						start <<= 1;
					if (start > opt::trimLen)
						start = opt::trimLen;

					// Wait for all the nodes to hit the checkpoint
					if(numRemoved == 0)
					{
						stopTrimming = true;
					}			

					m_numReachedCheckpoint++;
					while(!checkpointReached(m_numDataNodes))
					{
						pumpNetwork();
					}
					
					// All checkpoints are reached, reset the state
					SetState(NAS_TRIM);
				}
				
				// Trimming has been completed
				SetState(NAS_POPBUBBLE);
				m_pComm->SendControlMessage(m_numDataNodes, APC_POPBUBBLE);							
				break;
			}
			case NAS_POPBUBBLE:
			{
				popBubbles(this, kmerSize);
				m_numReachedCheckpoint++;
				while(!checkpointReached(m_numDataNodes))
				{
					pumpNetwork();
				}
				SetState(NAS_SPLIT);
				m_pComm->SendControlMessage(m_numDataNodes, APC_SPLIT);				
				break;
			}
			case NAS_SPLIT:
			{
				splitAmbiguous(this);
				m_numReachedCheckpoint++;
				while(!checkpointReached(m_numDataNodes))
				{
					pumpNetwork();
				}
				
				SetState(NAS_ASSEMBLE);			
				break;				
			}	
			case NAS_ASSEMBLE:
			{
				// Perform a round-robin assembly
				// The assembly operations cannot be concurrent since a contig can have a start in two different
				// nodes and would therefore result in a collision (and it would be output twice)
				// Using a round-robin assembly like this will have a minimal impact on performance since most of the heavy
				// computation is already done and the output of the contigs will mostly be bounded by file io

				// First, assemble the local sequences
				
				// Note: all other nodes will be in a waiting state so they will service network requests
				
				printf("%d: Assembling\n", m_id);
				
				// The master opens the file in truncate mode
				FastaWriter writer("pcontigs.fa");
				assemble(this, m_readLen, m_kmer, &writer);
				
				// Now tell all the slave nodes to perform the assemble one by one
				for(unsigned int i = 1; i < m_numDataNodes; ++i)
				{
					m_pComm->SendControlMessageToNode(i, APC_ASSEMBLE);
					
					// Wait for this node to return
					while(!checkpointReached(1))
					{
						pumpNetwork();
					}
					
					//Reset the state and loop
					SetState(NAS_ASSEMBLE);
				}
				
				SetState(NAS_DONE);
				m_pComm->SendControlMessage(m_numDataNodes, APC_FINISHED);				
				break;
			}
			case NAS_DONE:
			{
				printf("in done state\n");
				stop = true;
				break;
			}
			case NAS_WAITING:
			{
				break;
			}
			assert(false);							
		}
	}
}

//
// Set the state
//
void NetworkSequenceCollection::SetState(NetworkAssemblyState newState)
{
	m_state = newState;
	
	// Reset the checkpoint counter
	m_numReachedCheckpoint = 0;
}

APResult NetworkSequenceCollection::pumpNetwork()
{
	int senderID;
	m_pComm->flush();	
	//int numReceived = 0;
	bool stop = false;
	
	// Loop until either a) a APM_RESULT message is found in which case return and allow pump_until_result to handle it or b) no more messages are waiting
	while(!stop)
	{
		APMessage msg = m_pComm->CheckMessage(senderID);
		switch(msg)
		{
			case APM_SEQ:
				{
					parseSeqMessage(senderID);
					break;
				}
			case APM_SEQ_FLAG:
				{
					parseSeqFlagMessage(senderID);
					break;
				}
			case APM_SEQ_EXT:
				{
					parseSeqExtMessage(senderID);
					break;
				}
			case APM_CONTROL:
				{
					parseControlMessage(senderID);
					break;
				}
			case APM_RESULT_ADJ:
				{
					parseAdjacencyMessage(senderID);	
					break;
				}
			// APM_RESULT is handled in pumpUntilResult()				
			case APM_RESULT:
			case APM_NONE:
				{
					// either there is no message waiting or it is an AP_RESULT in which case pump_until_result will handle it
					stop = true;
					break;
				}
		}
		
	}
	return APR_NONE;
}

//
//
//
ResultPair NetworkSequenceCollection::pumpUntilResult()
{
	while(true)
	{
		// Service incoming requests
		pumpNetwork();
		
		// Check if the result has returned
		int senderID;
		m_pComm->flush();	
		APMessage msg = m_pComm->CheckMessage(senderID);
		
		// The result has arrived
		if(msg == APM_RESULT)
		{		
	
			//PrintDebug(0, "Result got\n");
			ResultMessage msg = m_pComm->ReceiveResultMessage();
			
			// Convert the result to a result pair
			ResultPair rp;
			rp.forward = (msg.result[0] == APR_TRUE) ? true : false;
			rp.reverse = (msg.result[1] == APR_TRUE) ? true : false;
			
			// Return the result
			return rp;
		}
	}	
}

void NetworkSequenceCollection::parseSeqMessage(int senderID)
{
	SeqMessage seqMsg = m_pComm->ReceiveSeqMessage();
	switch(seqMsg.operation)
	{
		case APO_ADD:
		{
			add(seqMsg.seq);
			break;
		}
		case APO_REMOVE:
		{
			remove(seqMsg.seq);
			break;
		}
		case APO_CHECKSEQ:
		{
			//PrintDebug(0, "Check seq got\n");
			assert(isLocal(seqMsg.seq));
			bool result = m_pLocalSpace->exists(seqMsg.seq);
			m_pComm->SendAdjacencyResult(senderID, seqMsg.id, result);
			break;
		}
		case APO_HAS_PARENT:
		{
			bool result = m_pLocalSpace->hasParent(seqMsg.seq);
			m_pComm->SendResultMessage(senderID, seqMsg.id, result);
			break;	
		}
		case APO_HAS_CHILD:
		{
			bool result = m_pLocalSpace->hasChild(seqMsg.seq);
			m_pComm->SendResultMessage(senderID, seqMsg.id, result);
			break;	
		}
	}	
}

//
//
//
void NetworkSequenceCollection::parseSeqFlagMessage(int senderID)
{
	SeqFlagMessage flagMsg = m_pComm->ReceiveSeqFlagMessage();
	switch(flagMsg.operation)
	{
		case APSFO_CHECK:
		{
			bool result = m_pLocalSpace->checkFlag(flagMsg.seq, flagMsg.flag);
			m_pComm->SendResultMessage(senderID, flagMsg.id, result);
			break;
		}
		case APSFO_SET:
		{
			m_pLocalSpace->setFlag(flagMsg.seq, flagMsg.flag);
			break;
		}
	}	
}

//
// Parse a sequence extension message
//
void NetworkSequenceCollection::parseSeqExtMessage(int senderID)
{
	SeqExtMessage extMsg = m_pComm->ReceiveSeqExtMessage();
	switch(extMsg.operation)
	{
		case APSEO_CHECK:
		{
			ResultPair result = m_pLocalSpace->checkExtension(extMsg.seq, extMsg.dir, extMsg.base);
			m_pComm->SendResultMessage(senderID, extMsg.id, result);
			break;
		}
		case APSEO_SET:
		{
			m_pLocalSpace->setExtension(extMsg.seq, extMsg.dir, extMsg.ext);
			break;
		}
		case APSEO_REMOVE:
		{
			m_pLocalSpace->removeExtension(extMsg.seq, extMsg.dir, extMsg.base);
			break;
		}
		case APSEO_CLEAR_ALL:
		{
			printf("Clearing extension\n");
			m_pLocalSpace->clearExtensions(extMsg.seq, extMsg.dir);
			break;
		}	
	}
}

//
//
//
void NetworkSequenceCollection::parseControlMessage(int /*senderID*/)
{
	ControlMessage controlMsg = m_pComm->ReceiveControlMessage();
	switch(controlMsg.msgType)
	{
		case APC_LOAD:
		{
			break;	
		}
		case APC_DONELOAD:
		{
			PrintDebug(0, "got done load message\n");
			SetState(NAS_FINALIZE);
			break;	
		}
		case APC_CHECKPOINT:
		{
			m_numReachedCheckpoint++;
			break;	
		}
		case APC_FINISHED:
		{
			SetState(NAS_DONE);
			PrintDebug(0, "finished message received, exiting\n");							
			break;	
		}
		case APC_TRIM:
		{
			// This message came from the control node along with an argument indicating the maximum branch to trim at
			m_trimStep = controlMsg.argument;
			SetState(NAS_TRIM);
			break;				
		}
		case APC_POPBUBBLE:
		{		
			SetState(NAS_POPBUBBLE);
			break;	
		}
		case APC_SPLIT:
		{
			SetState(NAS_SPLIT);
			break;	
		}
		case APC_ASSEMBLE:
		{
			// This message came from the control node along with an argument indicating the number of sequences assembled so far
			m_numAssembled = controlMsg.argument;			
			SetState(NAS_ASSEMBLE);
			break;	
		}
		case APC_GEN_ADJ:
		{
			SetState(NAS_GEN_ADJ);
			break;	
		}		
	}
}

int numAdjMessagesParsed = 0;
//
// Parse an adjacency result message
//
void NetworkSequenceCollection::parseAdjacencyMessage(int /*senderID*/)
{
	ResultMessage resultMsg = m_pComm->ReceiveResultMessage();
	uint64_t reqID = resultMsg.id;
	numAdjMessagesParsed++;
	// Look up the request in the set
	AdjacencyRequests::iterator iter = m_pendingRequests.find(reqID);
	
	// Make sure the request is valid
	assert(iter != m_pendingRequests.end());

	// Does the sequence exist?
	if(resultMsg.result[0] == APR_TRUE || resultMsg.result[1] == APR_TRUE)
	{
		adjSet++;
		setAdjacency(iter->second.origSeq, iter->second.dir, iter->second.base);	
	}
	
	// remove the request
	m_pendingRequests.erase(iter);
	
	//printf("%d: parse adj result, %d remain\n", m_id, m_pendingRequests.size());
	
}
int numAdjMessagesSent = 0;
int localAdj = 0;

//
// Generate adjacency - Network version
// This function operates in the same manner as AssemblyAlgorithms::GenerateAdjacency but has been rewritten to hide latency between nodes
//
void NetworkSequenceCollection::networkGenerateAdjacency(ISequenceCollection* seqCollection)
{
	printf("generating adjacency info - network\n");
	int count = 0;

	SequenceCollectionIterator endIter  = seqCollection->getEndIter();
	for(SequenceCollectionIterator iter = seqCollection->getStartIter(); iter != endIter; ++iter)
	{
		if(count % 10000 == 0)
		{
			printf("generated for %d\n", count);
		}
		count++;
		
		const PackedSeq& currSeq = *iter;
		//printf("gen for: %s\n", iter->decode().c_str());
		for(int i = 0; i <= 1; i++)
		{
			extDirection dir = (i == 0) ? SENSE : ANTISENSE;
			SeqExt extension;
			
			PackedSeq testSeq(currSeq);
			testSeq.rotate(dir, 'A');
			
			for(int j = 0; j < NUM_BASES; j++)
			{
				char currBase = BASES[j];
				testSeq.setLastBase(dir, currBase);
				
				// Here is the divergence from the common adjacency generation function
				// We only generate a request for the existance of the sequence at this moment and then carry on
				// When the data meanders over the network and eventually returns to use, THEN the adjacency is set
				// See:: SendAdjancencyRequest/ParseAdjancencyResponse
				computeAdjacency(currSeq, testSeq, dir, currBase);
			}	
		}
		
		// Simple load balancing, if there are too many pending requests, wait on the network
		const unsigned int MAX_NUM_PENDING = 200;
		if(m_pendingRequests.size() > MAX_NUM_PENDING)
		{
			while(m_pendingRequests.size() > 10)
			{
				pumpNetwork();
			}
		}
	}
	
	// Spin until the request queue is clear
	while(!m_pendingRequests.empty())
	{
		// Service the network
		seqCollection->pumpNetwork();
	}
	printf("Genereated %d adj\n", adjSet);
	printf("Sent %d adj mesagse\n", numAdjMessagesSent);
	printf("Parsed %d adj mesagse\n", numAdjMessagesParsed);
	printf("Local adj: %d\n", localAdj);
	printf("all requests filled, continue\n");
}


//
// SendAdjacencyRequest - Send an adjacent request over the network 
// 
void NetworkSequenceCollection::computeAdjacency(const PackedSeq& currSeq, const PackedSeq& requestSeq, extDirection dir, char base)
{
	// Check if the test sequence is local
	if(isLocal(requestSeq))
	{
		localAdj++;
		// simply look up the sequence in the local space
		if(m_pLocalSpace->exists(requestSeq))
		{
			// set adjacency
			setAdjacency(currSeq, dir, base);
		}	
	}
	else
	{
		numAdjMessagesSent++;
		// generate a request and send it off
		AdjancencyRequest request;
		request.origSeq = currSeq;
		request.requestSeq = requestSeq;
		request.dir = dir;
		request.base = base;
	
		// Send the request
		int nodeID = computeNodeID(requestSeq);
		assert(nodeID != m_id);
		// Add the request to the set
		uint64_t id = m_pComm->SendSeqMessage(nodeID, requestSeq, APO_CHECKSEQ);
		m_pendingRequests[id] = request;		
	}
}

//
// Set the adjacency of a sequence
//
void NetworkSequenceCollection::setAdjacency(const PackedSeq& seq, extDirection dir, char base)
{
	setBaseExtension(seq, dir, base);
}

//
//
//
void NetworkSequenceCollection::add(const PackedSeq& seq)
{
	// Check if this sequence is local
	if(isLocal(seq))
	{
		//PrintDebug(3, "received local seq: %s\n", seq.decode().c_str());
		m_pLocalSpace->add(seq);
		//PrintDebug(3, "done add\n");
	}
	else
	{
		int nodeID = computeNodeID(seq);		
		m_pComm->SendSeqMessage(nodeID, seq, APO_ADD);
	}
}

//
//
//
void NetworkSequenceCollection::remove(const PackedSeq& seq)
{
	// Check if this sequence is local
	if(isLocal(seq))
	{
		m_pLocalSpace->remove(seq);
	}
	else
	{
		
		int nodeID = computeNodeID(seq);	
		m_pComm->SendSeqMessage(nodeID, seq, APO_REMOVE);
	}	
}

//
//
//
void NetworkSequenceCollection::finalize()
{
	// this command is broadcast from the controller so we only perform a local finalize
	PrintDebug(1, "loaded: %d sequences\n", m_pLocalSpace->count());	
	m_pLocalSpace->finalize();
}

//
//
//
bool NetworkSequenceCollection::exists(const PackedSeq& seq)
{
	// Check if this sequence is local
	bool result;
	if(isLocal(seq))
	{
		result = m_pLocalSpace->exists(seq);
	}
	else
	{
		assert(false);
		/*
		//PrintDebug(1, "after send\n");
		ResultPair rp = pumpUntilResult();
		result = rp.forward || rp.reverse;
		*/
	}
	return result;
}

//
//
//
bool NetworkSequenceCollection::checkpointReached(int numRequired) const
{
	if(m_numReachedCheckpoint == numRequired)
	{
		return true;
	}
	else
	{
		return false;
	}
}

//
//
//
void NetworkSequenceCollection::setFlag(const PackedSeq& seq, SeqFlag flag)
{
	// Check if this sequence is local
	if(isLocal(seq))
	{
		//PrintDebug(3, "received local seq: %s\n", seq.decode().c_str());
		m_pLocalSpace->setFlag(seq, flag);
	}
	else
	{
		int nodeID = computeNodeID(seq);
		m_pComm->SendSeqFlagMessage(nodeID, seq, APSFO_SET, flag);
	}
}

//
//
//
bool NetworkSequenceCollection::checkFlag(const PackedSeq& seq, SeqFlag flag)
{
	// Check if this sequence is local
	bool result;
	if(isLocal(seq))
	{
		result = m_pLocalSpace->checkFlag(seq, flag);
	}
	else
	{
		int nodeID = computeNodeID(seq);
		//PrintDebug(1, "checking for non-local seq %s\n", seq.decode().c_str());
		m_pComm->SendSeqFlagMessage(nodeID, seq, APSFO_CHECK, flag);
		ResultPair rp = pumpUntilResult();
		result = rp.forward || rp.reverse;
	}
	return result;
}

//
//
// COMBINE THESE TWO FUNCTIONS
//
//

//
//
//
bool NetworkSequenceCollection::hasParent(const PackedSeq& seq)
{
	// Check if this sequence is local
	bool result;
	if(isLocal(seq))
	{
		result = m_pLocalSpace->hasParent(seq);
	}
	else
	{
		assert(false);
		int nodeID = computeNodeID(seq);
		//PrintDebug(1, "checking for non-local seq %s\n", seq.decode().c_str());
		m_pComm->SendSeqMessage(nodeID, seq, APO_HAS_PARENT);
		ResultPair rp = pumpUntilResult();
		result = rp.forward || rp.reverse;
	}
	return result;
}

//
//
//
bool NetworkSequenceCollection::hasChild(const PackedSeq& seq)
{
	// Check if this sequence is local
	bool result;
	if(isLocal(seq))
	{
		result = m_pLocalSpace->hasChild(seq);
	}
	else
	{
		assert(false);
		int nodeID = computeNodeID(seq);
		//PrintDebug(1, "checking for non-local seq %s\n", seq.decode().c_str());
		m_pComm->SendSeqMessage(nodeID, seq, APO_HAS_CHILD);
		ResultPair rp = pumpUntilResult();
		result = rp.forward || rp.reverse;
	}

	return result;
}

//
// set the extension for the sequence
//
void NetworkSequenceCollection::setExtension(const PackedSeq& seq, extDirection dir, SeqExt extension)
{
	// Check if this sequence is local
	if(isLocal(seq))
	{
		//PrintDebug(3, "received local seq: %s\n", seq.decode().c_str());
		m_pLocalSpace->setExtension(seq, dir, extension);
	}
	else
	{

		int nodeID = computeNodeID(seq);		
		// base is ignored for now
		m_pComm->SendSeqExtMessage(nodeID, seq, APSEO_SET, dir, extension);
	}
}

//
// 
//
void NetworkSequenceCollection::setBaseExtension(const PackedSeq& seq, extDirection dir, char base)
{
	if(isLocal(seq))
	{
		m_pLocalSpace->setBaseExtension(seq, dir, base);
	}
	else
	{	
		// This function should only be called locally
		assert(false);	
	}
}

//
// clear the extensions for the sequence
//
void NetworkSequenceCollection::clearExtensions(const PackedSeq& seq, extDirection dir)
{
	// Check if this sequence is local
	if(isLocal(seq))
	{
		//PrintDebug(3, "received local seq: %s\n", seq.decode().c_str());
		m_pLocalSpace->clearExtensions(seq, dir);
	}
	else
	{

		int nodeID = computeNodeID(seq);		
		// base is ignored for now
		SeqExt dummyExt;
		char dummyBase = 'A';
		m_pComm->SendSeqExtMessage(nodeID, seq, APSEO_CLEAR_ALL, dir, dummyExt, dummyBase);
	}
}

//
//
//
int NetworkSequenceCollection::count() const
{
	return m_pLocalSpace->count();
}

//
//
//
void NetworkSequenceCollection::removeExtension(const PackedSeq& seq, extDirection dir, char base)
{
	// Check if this sequence is local
	if(isLocal(seq))
	{
		//PrintDebug(3, "received local seq: %s\n", seq.decode().c_str());
		m_pLocalSpace->removeExtension(seq, dir, base);
	}
	else
	{
		int nodeID = computeNodeID(seq);
		SeqExt dummy;
		m_pComm->SendSeqExtMessage(nodeID, seq, APSEO_REMOVE, dir, dummy, base);
	}
}

//
//
//
ResultPair NetworkSequenceCollection::checkExtension(const PackedSeq& seq, extDirection dir, char base)
{
	// Check if this sequence is local
	ResultPair result;
	if(isLocal(seq))
	{
		//PrintDebug(1, "checking for local seq %s\n", seq.decode().c_str());
		result = m_pLocalSpace->checkExtension(seq, dir, base);
	}
	else
	{
		int nodeID = computeNodeID(seq);
		//PrintDebug(1, "checking for non-local seq %s\n", seq.decode().c_str());
		SeqExt dummy;
		m_pComm->SendSeqExtMessage(nodeID, seq, APSEO_CHECK, dir, dummy, base);
		result = pumpUntilResult();
	}

	return result;
}

//
// GetMultiplicity
//
int NetworkSequenceCollection::getMultiplicity(const PackedSeq& /*seq*/)
{
	// does nothing for now
	return 0;	
}

//
// The iterator functions return the local space's begin/end
//
SequenceCollectionIterator NetworkSequenceCollection::getStartIter() const
{
	return m_pLocalSpace->getStartIter();	
}

//
// The iterator functions return the local space's begin/end
//
SequenceCollectionIterator NetworkSequenceCollection::getEndIter() const
{
	return m_pLocalSpace->getEndIter();	
}

//
// Check if this sequence belongs in the local space
//
bool NetworkSequenceCollection::isLocal(const PackedSeq& seq) const
{
	int id = computeNodeID(seq);
	return id == m_id;
}

//
// 
//
int NetworkSequenceCollection::computeNodeID(const PackedSeq& seq) const
{
	unsigned int code = seq.getCode();
	unsigned int id = code % m_numDataNodes;	
	return id;
}

//
//
//
int NetworkSequenceCollection::PrintDebug(int level,char* fmt, ...) const
{	
	int retval=0;
	if(level <= 3)
	{
		printf("%d: ", m_id);
		va_list ap;
		va_start(ap, fmt); /* Initialize the va_list */
		retval = vprintf(fmt, ap); /* Call vprintf */
		
		va_end(ap); /* Cleanup the va_list */
	}

	return retval;
}

