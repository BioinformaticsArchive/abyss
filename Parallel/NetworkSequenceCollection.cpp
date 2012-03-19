#include "NetworkSequenceCollection.h"
#include "Assembly/Options.h"
#include "AssemblyAlgorithms.h"
#include "Common/Options.h"
#include "FastaWriter.h"
#include "Histogram.h"
#include "Log.h"
#include <climits> // for UINT_MAX
#include <cmath> // for roundf
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <utility>

using namespace std;

void NetworkSequenceCollection::loadSequences()
{
	Timer timer("LoadSequences");
	for (unsigned i = opt::rank; i < opt::inFiles.size();
			i += opt::numProc)
		AssemblyAlgorithms::loadSequences(this, opt::inFiles[i]);
}

/** Receive, process, send, and synchronize.
 * @return the number of inflight messages
 */
unsigned NetworkSequenceCollection::pumpFlushReduce()
{
	pumpNetwork();
	m_comm.flush();
	return m_comm.reduceInflight();
}

/** Receive packets and process them until no more work exists for any
 * slave processor.
 */
void NetworkSequenceCollection::completeOperation()
{
	Timer timer("completeOperation");

	while (pumpFlushReduce() > 0)
		;

	assert(m_comm.transmitBufferEmpty()); // Nothing to send.
	m_comm.barrier(); // Synchronize.
	assert(m_comm.receiveEmpty()); // Nothing to receive.
	assert(m_comm.reduceInflight() == 0);
}

/** Run the assembly state machine. */
void NetworkSequenceCollection::run()
{
	/** The number of contigs and k-mer assembled. */
	pair<unsigned, unsigned> numAssembled;

	ofstream bubbleFile;

	SetState(NAS_LOADING);
	while (m_state != NAS_DONE) {
		switch (m_state) {
			case NAS_LOADING:
				m_data.setColourSpace(
						m_comm.receiveBroadcast());
				loadSequences();
				EndState();
				SetState(NAS_WAITING);
				m_comm.sendCheckPointMessage();
				break;
			case NAS_LOAD_COMPLETE:
			{
				m_comm.barrier();
				pumpNetwork();
				PrintDebug(0, "Loaded %zu k-mer\n", m_data.size());
				m_data.shrink();
				m_comm.reduce(m_data.size());

				Histogram h(m_comm.reduce(
						AssemblyAlgorithms::coverageHistogram(
							m_data)));
				AssemblyAlgorithms::setCoverageParameters(h);
				EndState();
				SetState(NAS_WAITING);
				break;
			}
			case NAS_GEN_ADJ:
				m_comm.barrier();
				m_numBasesAdjSet = 0;
				AssemblyAlgorithms::generateAdjacency(this);
				EndState();
				SetState(NAS_WAITING);
				m_comm.sendCheckPointMessage();
				break;
			case NAS_ADJ_COMPLETE:
				m_comm.barrier();
				pumpNetwork();
				PrintDebug(0, "Generated %u edges\n",
						m_numBasesAdjSet);
				m_comm.reduce(m_numBasesAdjSet);
				EndState();
				SetState(NAS_WAITING);
				break;
			case NAS_ERODE:
			{
				m_comm.barrier();
				unsigned numEroded
					= AssemblyAlgorithms::erodeEnds(this);
				EndState();
				SetState(NAS_ERODE_WAITING);
				m_comm.sendCheckPointMessage(numEroded);
				break;
			}
			case NAS_ERODE_WAITING:
				pumpNetwork();
				break;
			case NAS_ERODE_COMPLETE:
				completeOperation();
				m_comm.reduce(AssemblyAlgorithms::getNumEroded());

				m_comm.reduce(m_data.cleanup());
				m_comm.barrier();

				SetState(NAS_WAITING);
				break;
			case NAS_TRIM:
			{
				assert(m_trimStep != 0);
				m_comm.barrier();
				int numRemoved = performNetworkTrim(this);
				EndState();
				SetState(NAS_WAITING);
				m_comm.sendCheckPointMessage(numRemoved);
				break;
			}
			case NAS_REMOVE_MARKED: {
				m_comm.barrier();
				unsigned count
					= AssemblyAlgorithms::removeMarked(this);
				EndState();
				SetState(NAS_WAITING);
				m_comm.sendCheckPointMessage(count);
				break;
			}

			case NAS_COVERAGE:
			{
				m_comm.reduce(m_data.cleanup());
				m_lowCoverageContigs = 0;
				m_lowCoverageKmer = 0;
				numAssembled = performNetworkAssembly(this);
				EndState();
				SetState(NAS_WAITING);
				m_comm.sendCheckPointMessage();
				break;
			}
			case NAS_COVERAGE_COMPLETE:
				m_comm.barrier();
				pumpNetwork();
				m_comm.reduce(numAssembled.first);
				m_comm.reduce(numAssembled.second);
				m_comm.reduce(m_lowCoverageContigs);
				m_comm.reduce(m_lowCoverageKmer);
				opt::coverage = 0;
				EndState();
				SetState(NAS_WAITING);
				break;

			case NAS_DISCOVER_BUBBLES:
			{
				unsigned numDiscovered
					= performNetworkDiscoverBubbles(this);
				EndState();
				SetState(NAS_WAITING);
				m_comm.sendCheckPointMessage(numDiscovered);
				break;
			}
			case NAS_POPBUBBLE:
			{
				if (!bubbleFile.is_open())
					AssemblyAlgorithms::openBubbleFile(bubbleFile);
				unsigned numPopped
					= performNetworkPopBubbles(bubbleFile);
				EndState();
				SetState(NAS_WAITING);
				m_comm.sendCheckPointMessage(numPopped);
				break;
			}
			case NAS_MARK_AMBIGUOUS:
			{
				m_comm.barrier();
				pumpNetwork();
				unsigned count
					= AssemblyAlgorithms::markAmbiguous(this);
				EndState();
				SetState(NAS_WAITING);
				m_comm.sendCheckPointMessage(count);
				break;
			}
			case NAS_SPLIT_AMBIGUOUS:
			{
				m_comm.barrier();
				assert(m_comm.receiveEmpty());
				unsigned count
					= AssemblyAlgorithms::splitAmbiguous(this);
				EndState();
				SetState(NAS_WAITING);
				m_comm.sendCheckPointMessage(count);
				break;
			}
			case NAS_CLEAR_FLAGS:
				m_comm.barrier();
				assert(m_comm.receiveEmpty());
				m_data.wipeFlag(
						SeqFlag(SF_MARK_SENSE | SF_MARK_ANTISENSE));
				m_comm.reduce(m_data.cleanup());
				EndState();
				SetState(NAS_WAITING);
				break;
			case NAS_ASSEMBLE:
			{
				m_comm.barrier();
				pumpNetwork();
				FastaWriter writer(opt::contigsTempPath.c_str());
				numAssembled = performNetworkAssembly(this, &writer);
				EndState();
				SetState(NAS_WAITING);
				m_comm.sendCheckPointMessage();
				break;
			}
			case NAS_ASSEMBLE_COMPLETE:
				m_comm.reduce(numAssembled.first);
				m_comm.reduce(numAssembled.second);
				EndState();
				SetState(NAS_DONE);
				break;
			case NAS_WAITING:
				pumpNetwork();
				break;
			case NAS_DONE:
				break;
		}
	}
}

unsigned NetworkSequenceCollection::controlErode()
{
	SetState(NAS_ERODE);
	m_comm.sendControlMessage(APC_SET_STATE, NAS_ERODE);
	m_comm.barrier();
	unsigned numEroded = AssemblyAlgorithms::erodeEnds(this);
	EndState();

	// Do not call SetState, because it clears the
	// checkpoint information.
	//SetState(NAS_ERODE_WAITING);
	m_state = NAS_ERODE_WAITING;

	m_numReachedCheckpoint++;
	while (!checkpointReached())
		pumpNetwork();
	numEroded += m_checkpointSum;
	EndState();

	if (numEroded == 0) {
		SetState(NAS_WAITING);
		m_comm.sendControlMessage(APC_WAIT);
		m_comm.barrier();
		return 0;
	}

	SetState(NAS_ERODE_COMPLETE);
	m_comm.sendControlMessage(APC_ERODE_COMPLETE);
	completeOperation();
	numEroded += m_comm.reduce(
			AssemblyAlgorithms::getNumEroded());
	printf("Eroded %u tips\n", numEroded);

	unsigned removed = m_comm.reduce(m_data.cleanup());
	m_comm.barrier();
	assert(removed == numEroded);
	(void)removed;

	SetState(NAS_WAITING);
	return numEroded;
}

/** Remove marked k-mer.
 * @return the number of k-mer removed
 */
unsigned NetworkSequenceCollection::controlRemoveMarked()
{
	if (opt::verbose > 0)
		puts("Sweeping");
	SetState(NAS_REMOVE_MARKED);
	m_comm.sendControlMessage(APC_SET_STATE, NAS_REMOVE_MARKED);
	m_comm.barrier();
	unsigned count = AssemblyAlgorithms::removeMarked(this);
	m_checkpointSum += count;
	EndState();

	m_numReachedCheckpoint++;
	while (!checkpointReached())
		pumpNetwork();
	return m_checkpointSum;
}

/** Perform a single round of trimming at the specified length. */
unsigned NetworkSequenceCollection::controlTrimRound(unsigned trimLen)
{
	assert(trimLen > 0);
	m_trimStep = trimLen;
	printf("Trimming short branches: %u\n", trimLen);
	SetState(NAS_TRIM);
	m_comm.sendControlMessage(APC_TRIM, trimLen);
	m_comm.barrier();
	unsigned numRemoved = performNetworkTrim(this);
	EndState();

	m_numReachedCheckpoint++;
	while (!checkpointReached())
		pumpNetwork();
	numRemoved += m_checkpointSum;

	unsigned numSweeped = controlRemoveMarked();

	if (numRemoved > 0)
		printf("Trimmed %u k-mer in %u branches\n",
				numSweeped, numRemoved);
	return numRemoved;
}

/** Perform multiple rounds of trimming until complete. */
void NetworkSequenceCollection::controlTrim(unsigned start)
{
	if (opt::trimLen == 0)
		return;
	unsigned rounds = 0, total = 0;
	for (int trim = start; trim < opt::trimLen; trim *= 2) {
		rounds++;
		total += controlTrimRound(trim);
	}
	unsigned count;
	while ((count = controlTrimRound(opt::trimLen)) > 0) {
		rounds++;
		total += count;
	}
	printf("Trimmed %u branches in %u rounds\n", total, rounds);
}

/** Remove low-coverage contigs. */
void NetworkSequenceCollection::controlCoverage()
{
	assert(opt::coverage > 0);

	// Split ambiguous branches.
	SetState(NAS_MARK_AMBIGUOUS);
	controlMarkAmbiguous();

	// Remove low-coverage contigs.
	printf("Removing low-coverage contigs "
			"(mean k-mer coverage < %f)\n", opt::coverage);
	SetState(NAS_COVERAGE);
	m_comm.sendControlMessage(APC_SET_STATE, NAS_COVERAGE);
	m_comm.reduce(m_data.cleanup());
	m_lowCoverageContigs = 0;
	m_lowCoverageKmer = 0;
	pair<unsigned, unsigned> numAssembled
		= performNetworkAssembly(this);
	EndState();

	m_numReachedCheckpoint++;
	while (!checkpointReached())
		pumpNetwork();

	// Count the number of low-coverage contigs.
	SetState(NAS_COVERAGE_COMPLETE);
	m_comm.sendControlMessage(APC_SET_STATE, NAS_COVERAGE_COMPLETE);
	m_comm.barrier();
	pumpNetwork();

	numAssembled.first = m_comm.reduce(numAssembled.first);
	numAssembled.second = m_comm.reduce(numAssembled.second);
	printf("Found %u k-mer in %u contigs "
			"before removing low-coverage contigs\n",
			numAssembled.second, numAssembled.first);

	unsigned lowCoverageContigs = m_comm.reduce(m_lowCoverageContigs);
	unsigned lowCoverageKmer = m_comm.reduce(m_lowCoverageKmer);
	printf("Removed %u k-mer in %u low-coverage contigs\n",
			lowCoverageKmer, lowCoverageContigs);
	EndState();

	SetState(NAS_SPLIT_AMBIGUOUS);
	controlSplitAmbiguous();

	SetState(NAS_CLEAR_FLAGS);
	m_comm.sendControlMessage(APC_SET_STATE, NAS_CLEAR_FLAGS);
	m_comm.barrier();
	assert(m_comm.receiveEmpty());
	m_data.wipeFlag(SeqFlag(SF_MARK_SENSE | SF_MARK_ANTISENSE));
	unsigned removed = m_comm.reduce(m_data.cleanup());
	printf("Removed %u marked k-mer\n", removed);
	EndState();

	opt::coverage = 0;
}

/** Run the assembly state machine for the controller (rank = 0). */
void NetworkSequenceCollection::runControl()
{
	SetState(NAS_LOADING);
	while (m_state != NAS_DONE) {
		switch (m_state) {
			case NAS_LOADING:
			{
				loadSequences();
				assert(m_data.size() > 0);
				EndState();

				m_numReachedCheckpoint++;
				while (!checkpointReached())
					pumpNetwork();

				SetState(NAS_LOAD_COMPLETE);
				m_comm.sendControlMessage(APC_SET_STATE,
						NAS_LOAD_COMPLETE);
				m_comm.barrier();
				pumpNetwork();
				PrintDebug(0, "Loaded %zu k-mer\n", m_data.size());
				m_data.shrink();
				printf("Loaded %lu k-mer\n",
						m_comm.reduce(m_data.size()));

				Histogram h(m_comm.reduce(
						AssemblyAlgorithms::coverageHistogram(
							m_data)));
				AssemblyAlgorithms::setCoverageParameters(h);
				EndState();

				SetState(m_data.isAdjacencyLoaded()
						? NAS_ERODE : NAS_GEN_ADJ);
				break;
			}
			case NAS_GEN_ADJ:
				puts("Generating adjacency");
				m_comm.sendControlMessage(APC_SET_STATE, NAS_GEN_ADJ);
				m_comm.barrier();
				m_numBasesAdjSet = 0;
				AssemblyAlgorithms::generateAdjacency(this);
				EndState();

				m_numReachedCheckpoint++;
				while (!checkpointReached())
					pumpNetwork();

				SetState(NAS_ADJ_COMPLETE);
				m_comm.sendControlMessage(APC_SET_STATE,
						NAS_ADJ_COMPLETE);
				m_comm.barrier();
				pumpNetwork();
				PrintDebug(0, "Generated %u edges\n",
						m_numBasesAdjSet);
				printf("Generated %lu edges\n",
						m_comm.reduce(m_numBasesAdjSet));
				EndState();

				SetState(opt::erode > 0 ? NAS_ERODE : NAS_TRIM);
				break;
			case NAS_ERODE:
				assert(opt::erode > 0);
				puts("Eroding tips");
				controlErode();
				SetState(NAS_TRIM);
				break;

			case NAS_LOAD_COMPLETE:
			case NAS_ADJ_COMPLETE:
			case NAS_REMOVE_MARKED:
			case NAS_ERODE_WAITING:
			case NAS_ERODE_COMPLETE:
			case NAS_COVERAGE_COMPLETE:
			case NAS_SPLIT_AMBIGUOUS:
			case NAS_CLEAR_FLAGS:
			case NAS_DISCOVER_BUBBLES:
			case NAS_ASSEMBLE_COMPLETE:
			case NAS_WAITING:
				// These states are used only by the slaves.
				assert(false);
				exit(EXIT_FAILURE);

			case NAS_TRIM:
				controlTrim();
				SetState(opt::coverage > 0 ? NAS_COVERAGE
						: opt::bubbleLen > 0 ? NAS_POPBUBBLE
						: NAS_MARK_AMBIGUOUS);
				break;

			case NAS_COVERAGE:
				controlCoverage();
				SetState(NAS_ERODE);
				break;

			case NAS_POPBUBBLE:
			{
				assert(opt::bubbleLen > 0);
				ofstream out;
				AssemblyAlgorithms::openBubbleFile(out);

				puts("Popping bubbles");
				unsigned numPopped = controlPopBubbles(out);
				assert(numPopped == m_numPopped);
				assert(out.good());
				out.close();
				printf("Removed %u bubbles\n", numPopped);

				SetState(NAS_MARK_AMBIGUOUS);
				break;
			}
			case NAS_MARK_AMBIGUOUS:
				controlMarkAmbiguous();
				SetState(NAS_ASSEMBLE);
				break;
			case NAS_ASSEMBLE:
			{
				puts("Assembling");
				m_comm.sendControlMessage(APC_ASSEMBLE);
				m_comm.barrier();
				pumpNetwork();
				FastaWriter writer(opt::contigsTempPath.c_str());
				pair<unsigned, unsigned> numAssembled
					= performNetworkAssembly(this, &writer);
				EndState();

				m_numReachedCheckpoint++;
				while (!checkpointReached())
					pumpNetwork();

				SetState(NAS_ASSEMBLE_COMPLETE);
				m_comm.sendControlMessage(APC_SET_STATE,
						NAS_ASSEMBLE_COMPLETE);

				numAssembled.first = m_comm.reduce(
						numAssembled.first);
				numAssembled.second = m_comm.reduce(
						numAssembled.second);
				printf("Assembled %u k-mer in %u contigs\n",
						numAssembled.second, numAssembled.first);

				SetState(NAS_DONE);
				break;
			}
			case NAS_DONE:
				break;
		}
	}
}

void NetworkSequenceCollection::EndState()
{
	// Flush the message buffer
	m_comm.flush();
}

//
// Set the state
//
void NetworkSequenceCollection::SetState(NetworkAssemblyState newState)
{
	PrintDebug(2, "SetState %u (was %u)\n", newState, m_state);

	// Ensure there are no pending messages
	assert(m_comm.transmitBufferEmpty());

	m_state = newState;

	// Reset the checkpoint counter
	m_numReachedCheckpoint = 0;
	m_checkpointSum = 0;
}

/** Receive and dispatch packets.
 * @return the number of packets received
 */
unsigned NetworkSequenceCollection::pumpNetwork()
{
	for (unsigned count = 0; ; count++) {
		int senderID;
		APMessage msg = m_comm.checkMessage(senderID);
		switch(msg)
		{
			case APM_CONTROL:
				parseControlMessage(senderID);
				// Deal with the control packet before we continue
				// processing further packets.
				return ++count;
			case APM_BUFFERED:
				{
					MessagePtrVector msgs;
					m_comm.receiveBufferedMessage(msgs);
					for(MessagePtrVector::iterator iter = msgs.begin(); iter != msgs.end(); iter++)
					{
						// Handle each message based on its type
						(*iter)->handle(senderID, *this);
						// Delete the message
						delete (*iter);
						*iter = 0;
					}
					break;
				}
			case APM_NONE:
				return count;
		}
	}
}

/** Call the observers of the specified sequence. */
void NetworkSequenceCollection::notify(const Kmer& key)
{
	switch (m_state) {
		case NAS_ERODE:
		case NAS_ERODE_WAITING:
		case NAS_ERODE_COMPLETE:
			AssemblyAlgorithms::erode(this,
					m_data.getSeqAndData(key));
			break;
		default:
			// Nothing to do.
			break;
	}
}

void NetworkSequenceCollection::handle(
		int /*senderID*/, const SeqAddMessage& message)
{
	assert(isLocal(message.m_seq));
	m_data.add(message.m_seq);
}

void NetworkSequenceCollection::handle(
		int /*senderID*/, const SeqRemoveMessage& message)
{
	assert(isLocal(message.m_seq));
	m_data.remove(message.m_seq);
}

void NetworkSequenceCollection::handle(
		int /*senderID*/, const SetFlagMessage& message)
{
	assert(isLocal(message.m_seq));
	m_data.setFlag(message.m_seq, (SeqFlag)message.m_flag);
}

void NetworkSequenceCollection::handle(
		int /*senderID*/, const SetBaseMessage& message)
{
	assert(isLocal(message.m_seq));
	setBaseExtension(message.m_seq, (extDirection)message.m_dir,
			message.m_base);
}

void NetworkSequenceCollection::handle(
		int /*senderID*/, const RemoveExtensionMessage& message)
{
	assert(isLocal(message.m_seq));
	m_data.removeExtension(message.m_seq,
			(extDirection)message.m_dir, message.m_ext);
	notify(message.m_seq);
}

void NetworkSequenceCollection::parseControlMessage(int source)
{
	ControlMessage controlMsg = m_comm.receiveControlMessage();
	switch(controlMsg.msgType)
	{
		case APC_SET_STATE:
			SetState(NetworkAssemblyState(controlMsg.argument));
			break;
		case APC_CHECKPOINT:
			PrintDebug(4, "checkpoint from %u: %u\n",
					source, controlMsg.argument);
			m_numReachedCheckpoint++;
			m_checkpointSum += controlMsg.argument;
			break;
		case APC_WAIT:
			SetState(NAS_WAITING);
			m_comm.barrier();
			break;
		case APC_BARRIER:
			assert(m_state == NAS_WAITING);
			m_comm.barrier();
			break;
		case APC_TRIM:
			m_trimStep = controlMsg.argument;
			SetState(NAS_TRIM);
			break;
		case APC_ERODE_COMPLETE:
			assert(m_state == NAS_ERODE_WAITING);
			m_comm.flush();
			SetState(NAS_ERODE_COMPLETE);
			break;
		case APC_POPBUBBLE:
			m_numPopped = controlMsg.argument;
			SetState(NAS_POPBUBBLE);
			break;
		case APC_ASSEMBLE:
			m_numAssembled = controlMsg.argument;
			SetState(NAS_ASSEMBLE);
			break;
	}
}

void NetworkSequenceCollection::handle(
		int senderID, const SeqDataRequest& message)
{
	const Kmer& kmer = message.m_seq;
	assert(isLocal(kmer));
	ExtensionRecord extRec;
	int multiplicity = -1;
	bool found = m_data.getSeqData(kmer, extRec, multiplicity);
	assert(found);
	(void)found;
	m_comm.sendSeqDataResponse(senderID, message.m_group, message.m_id,
			kmer, extRec, multiplicity);
}

void NetworkSequenceCollection::handle(
		int /*senderID*/, const SeqDataResponse& message)
{
	processSequenceExtension(message.m_group, message.m_id, message.m_seq, message.m_extRecord, message.m_multiplicity);
}

/** Distributed trimming function. */
int NetworkSequenceCollection::performNetworkTrim(
		ISequenceCollection* seqCollection)
{
	Timer timer("NetworkTrim");
	int numBranchesRemoved = 0;

	// The branch ids
	uint64_t branchGroupID = 0;

	for (ISequenceCollection::iterator iter = seqCollection->begin();
			iter != seqCollection->end(); ++iter) {
		if (iter->second.deleted())
			continue;

		extDirection dir;
		// dir will be set to the trimming direction if the sequence can be trimmed
		SeqContiguity status = AssemblyAlgorithms::checkSeqContiguity(
				*iter, dir);
		if (status == SC_CONTIGUOUS)
			continue;
		else if(status == SC_ISLAND)
		{
			seqCollection->mark(iter->first);
			numBranchesRemoved++;
			continue;
		}

		bool inserted = m_activeBranchGroups.insert(
				BranchGroupMap::value_type(branchGroupID,
					BranchGroup(dir, 1, iter->first,
						BranchRecord(dir))))
			.second;
		assert(inserted);
		(void)inserted;

		generateExtensionRequest(branchGroupID, 0, iter->first);
		branchGroupID++;
		numBranchesRemoved += processBranchesTrim();
		seqCollection->pumpNetwork();

		// Primitive load balancing
		if(m_activeBranchGroups.size() > MAX_ACTIVE)
		{
			while(m_activeBranchGroups.size() > LOW_ACTIVE)
			{
				seqCollection->pumpNetwork();
				numBranchesRemoved += processBranchesTrim();
			}
		}
	}
	
	// Clear out the remaining branches
	while(!m_activeBranchGroups.empty())
	{
		numBranchesRemoved += processBranchesTrim();
		seqCollection->pumpNetwork();
	}

	PrintDebug(0, "Trimmed %u branches\n", numBranchesRemoved);
	return numBranchesRemoved;
}

//
// Process current branches, removing those that are finished
// returns true if the branch list has branches remaining
//
int NetworkSequenceCollection::processBranchesTrim()
{
	int numBranchesRemoved = 0;
	vector<BranchGroupMap::iterator> removeBranches;
	// Check if any of the current branches have gone inactive
	for(BranchGroupMap::iterator iter = m_activeBranchGroups.begin(); iter != m_activeBranchGroups.end(); iter++)
	{
		if(!iter->second.isActive())
		{
			assert(iter->second.size() == 1);
			if (AssemblyAlgorithms::processTerminatedBranchTrim(
						this, iter->second[0]))
				numBranchesRemoved++;

			// Mark the group for removal
			removeBranches.push_back(iter);
		}
	}

	// Remove all the finished branches
	for (vector<BranchGroupMap::iterator>::iterator rmIter
				= removeBranches.begin();
			rmIter != removeBranches.end(); rmIter++)
		m_activeBranchGroups.erase(*rmIter);
	return numBranchesRemoved;
}

/** Discover bubbles to pop. */
int NetworkSequenceCollection::performNetworkDiscoverBubbles(ISequenceCollection* seqCollection)
{
	Timer timer("NetworkDiscoverBubbles");
	
	// The branch ids
	uint64_t branchGroupID = 0;
	m_finishedGroups.clear();
	
	// make sure the branch group structure is initially empty
	assert(m_activeBranchGroups.empty());
	
	int count = 0;

	// Set the cutoffs
	const unsigned int maxNumBranches = 3;

	for (ISequenceCollection::iterator iter = seqCollection->begin();
			iter != seqCollection->end(); ++iter) {
		if (iter->second.deleted())
			continue;

		if (++count % 100000 == 0)
			PrintDebug(1, "Popping bubbles: %u k-mer\n", count);

		ExtensionRecord extRec = iter->second.extension();
		for (extDirection dir = SENSE; dir <= ANTISENSE; ++dir) {
			if (extRec.dir[dir].isAmbiguous()) {
				BranchGroupMap::iterator groupIter
					= m_activeBranchGroups.insert(
						BranchGroupMap::value_type(branchGroupID,
							BranchGroup(dir, maxNumBranches,
								iter->first))).first;
				BranchGroup& group = groupIter->second;
				AssemblyAlgorithms::initiateBranchGroup(
						group, iter->first, extRec.dir[dir]);
				generateExtensionRequests(branchGroupID++,
						group.begin(), group.end());
			}
		}

		// Primitive load balancing
		if (m_activeBranchGroups.size() > MAX_ACTIVE) {
			while (m_activeBranchGroups.size() > LOW_ACTIVE) {
				seqCollection->pumpNetwork();
				processBranchesDiscoverBubbles();
			}
		}

		processBranchesDiscoverBubbles();
		seqCollection->pumpNetwork();
	}
	
	// Wait until the groups finish extending.
	while (processBranchesDiscoverBubbles())
		seqCollection->pumpNetwork();
	assert(m_activeBranchGroups.empty());

	unsigned numDiscovered = m_bubbles.size();
	PrintDebug(1, "Discovered %u bubbles\n", numDiscovered);
	return numDiscovered;
}

/** Pop bubbles discovered previously. */
int NetworkSequenceCollection::performNetworkPopBubbles(ostream& out)
{
	Timer timer("NetworkPopBubbles");

	// Deal with any packets still in the queue. The barrier
	// synchronization guarantees that the packets have been
	// delivered, but we may not have dealt with them yet.
	pumpNetwork();
	assert(m_comm.receiveEmpty());

	unsigned numPopped = 0;
	for (BranchGroupMap::iterator iter = m_bubbles.begin();
			iter != m_bubbles.end(); iter++) {
		assert(iter->second.getStatus() == BGS_JOINED);
		// Check whether this bubble has already been popped.
		if (!iter->second.isAmbiguous(m_data))
			continue;
		numPopped++;
		AssemblyAlgorithms::writeBubble(out,
				iter->second, m_numPopped + numPopped);
		AssemblyAlgorithms::collapseJoinedBranches(
				this, iter->second);
		assert(!iter->second.isAmbiguous(m_data));
		assert(m_comm.receiveEmpty());
	}
	m_bubbles.clear();
	out.flush();
	assert(out.good());

	PrintDebug(0, "Removed %u bubbles\n", numPopped);
	return numPopped;
}

//
// Process groups that are finished searching for bubbles
//
bool NetworkSequenceCollection::processBranchesDiscoverBubbles()
{
	bool active = false;
	// Check if any of the current branches have gone inactive
	BranchGroupMap::iterator iter = m_activeBranchGroups.begin();
	while (iter != m_activeBranchGroups.end()) {
		// All branches have been extended one sequence. Check the
		// stop conditions. updateStatus() is called in
		// processSequenceExtensionPop().
		BranchGroupStatus status = iter->second.isNoExt() ? BGS_NOEXT
			: iter->second.getStatus();
		bool finished = false;
		switch (status) {
			case BGS_TOOLONG:
			case BGS_TOOMANYBRANCHES:
			case BGS_NOEXT:
				finished = true;
				break;
			case BGS_JOINED:
				m_bubbles.insert(*iter);
				finished = true;
				break;
			case BGS_ACTIVE:
				active = true;
				break;
			default:
				assert(false);
		}
		if (finished) {
			m_finishedGroups.insert(iter->first);
			m_activeBranchGroups.erase(iter++);
		} else
			iter++;
	}
	return active;
}

/** Discover bubbles to pop. */
unsigned NetworkSequenceCollection::controlDiscoverBubbles()
{
	SetState(NAS_DISCOVER_BUBBLES);
	m_comm.sendControlMessage(APC_SET_STATE, NAS_DISCOVER_BUBBLES);

	unsigned numDiscovered = performNetworkDiscoverBubbles(this);
	EndState();

	m_numReachedCheckpoint++;
	while (!checkpointReached())
		pumpNetwork();
	numDiscovered += m_checkpointSum;
	SetState(NAS_POPBUBBLE);
	if (numDiscovered > 0 && opt::verbose > 0)
		printf("Discovered %u bubbles\n", numDiscovered);
	return numDiscovered;
}

/** Pop the bubbles discovered previously. */
int NetworkSequenceCollection::controlPopBubbles(ostream& out)
{
	controlDiscoverBubbles();

	// Perform a round-robin bubble pop to avoid concurrency issues
	m_checkpointSum = performNetworkPopBubbles(out);
	EndState();

	// Now tell all the slave nodes to perform the pop one by one
	for(int i = 1; i < opt::numProc; i++) {
		m_comm.sendControlMessage(APC_BARRIER);
		m_comm.barrier();
		m_numReachedCheckpoint = 0;
		m_comm.sendControlMessageToNode(i, APC_POPBUBBLE,
				m_numPopped + m_checkpointSum);
		while (!checkpointReached(1))
			pumpNetwork();
	}

	unsigned numPopped = m_checkpointSum;
	m_numPopped += numPopped;
	if (numPopped > 0)
		printf("Removed %u bubbles\n", numPopped);
	return numPopped;
}

/** Mark ambiguous branches. */
unsigned NetworkSequenceCollection::controlMarkAmbiguous()
{
	puts("Marking ambiguous branches");
	m_comm.sendControlMessage(APC_SET_STATE, NAS_MARK_AMBIGUOUS);
	m_comm.barrier();
	pumpNetwork();
	unsigned count = AssemblyAlgorithms::markAmbiguous(this);
	m_checkpointSum += count;
	EndState();
	m_numReachedCheckpoint++;
	while (!checkpointReached())
		pumpNetwork();
	printf("Marked %u ambiguous branches\n", m_checkpointSum);
	return m_checkpointSum;
}

/** Remove ambiguous branches. */
unsigned NetworkSequenceCollection::controlSplitAmbiguous()
{
	puts("Splitting ambiguous branches");
	m_comm.sendControlMessage(APC_SET_STATE, NAS_SPLIT_AMBIGUOUS);
	m_comm.barrier();
	assert(m_comm.receiveEmpty());
	unsigned count = AssemblyAlgorithms::splitAmbiguous(this);
	m_checkpointSum += count;
	EndState();
	m_numReachedCheckpoint++;
	while (!checkpointReached())
		pumpNetwork();
	printf("Split %u ambiguous branches\n",
			m_checkpointSum);
	return m_checkpointSum;
}

/** Assemble a contig. */
void NetworkSequenceCollection::assembleContig(
		ISequenceCollection* seqCollection, FastaWriter* writer,
		BranchRecord& branch, unsigned id)
{
	unsigned removed = AssemblyAlgorithms::assembleContig(
			seqCollection, writer, branch, id);
	if (removed > 0) {
		m_lowCoverageContigs++;
		m_lowCoverageKmer += removed;
	}
}

namespace std {
	pair<unsigned, unsigned>& operator +=(pair<unsigned, unsigned>& a,
			pair<unsigned, unsigned> b)
	{
		a.first += b.first;
		a.second += b.second;
		return a;
	}
}

/** Assemble contigs.
 * @return the number of contigs and k-mer assembled
 */
pair<unsigned, unsigned> NetworkSequenceCollection::
performNetworkAssembly(ISequenceCollection* seqCollection,
		FastaWriter* fileWriter)
{
	Timer timer("NetworkAssembly");
	pair<unsigned, unsigned> numAssembled(0, 0);
	uint64_t branchGroupID = 0;
	assert(m_activeBranchGroups.empty());

	for (ISequenceCollection::iterator iter = seqCollection->begin();
			iter != seqCollection->end(); ++iter) {
		if (iter->second.deleted())
			continue;

		extDirection dir;
		// dir will be set to the assembly direction if the sequence can be assembled
		SeqContiguity status = AssemblyAlgorithms::checkSeqContiguity(
				*iter, dir, true);
		if (status == SC_CONTIGUOUS)
			continue;
		else if(status == SC_ISLAND)
		{
			// Output the singleton contig.
			BranchRecord currBranch(SENSE);
			currBranch.push_back(*iter);
			currBranch.terminate(BS_NOEXT);
			assembleContig(seqCollection, fileWriter, currBranch,
					m_numAssembled + numAssembled.first);
			numAssembled.first++;
			numAssembled.second += currBranch.size();
			continue;
		}

		BranchGroup group(dir, 1, iter->first);
		group.addBranch(BranchRecord(dir));
		pair<BranchGroupMap::iterator, bool>
			inserted = m_activeBranchGroups.insert(
				BranchGroupMap::value_type(branchGroupID, group));
		assert(inserted.second);

		// Generate the first extension request
		BranchRecord& branch = inserted.first->second[0];
		branch.push_back(*iter);
		Kmer kmer = iter->first;
		AssemblyAlgorithms::extendBranch(branch,
				kmer, iter->second.getExtension(dir));
		assert(branch.isActive());
		generateExtensionRequest(branchGroupID++, 0, kmer);

		numAssembled += processBranchesAssembly(seqCollection,
				fileWriter, numAssembled.first);
		seqCollection->pumpNetwork();

		if(m_activeBranchGroups.size() > MAX_ACTIVE)
		{
			while(m_activeBranchGroups.size() > LOW_ACTIVE)
			{
				seqCollection->pumpNetwork();
				numAssembled += processBranchesAssembly(seqCollection,
						fileWriter, numAssembled.first);
			}
		}
	}
	
	// Clear out the remaining branches
	while(!m_activeBranchGroups.empty())
	{
		numAssembled += processBranchesAssembly(seqCollection,
				fileWriter, numAssembled.first);
		seqCollection->pumpNetwork();
	}

	if (opt::coverage > 0) {
		PrintDebug(0, "Found %u k-mer in %u contigs before removing "
				"low-coverage contigs\n",
				numAssembled.second, numAssembled.first);
		PrintDebug(0, "Removed %u k-mer in %u low-coverage contigs\n",
				m_lowCoverageKmer, m_lowCoverageContigs);
	} else
		PrintDebug(0, "Assembled %u k-mer in %u contigs\n",
				numAssembled.second, numAssembled.first);
	return numAssembled;
}

/** Processes branches that are in progress, removing those that have
 * completed.
 * @return the number of contigs and k-mer assembled
 */
pair<unsigned, unsigned> NetworkSequenceCollection::
processBranchesAssembly(ISequenceCollection* seqCollection,
		FastaWriter* fileWriter, int currContigID)
{
	unsigned assembledContigs = 0, assembledKmer = 0;
	for (BranchGroupMap::iterator it = m_activeBranchGroups.begin();
			it != m_activeBranchGroups.end();) {
		if (!it->second.isActive()) {
			assert(it->second.size() == 1);
			BranchRecord& branch = it->second[0];
			assert(branch.getState() == BS_NOEXT
					|| branch.getState() == BS_AMBI_SAME
					|| branch.getState() == BS_AMBI_OPP);
			if (branch.isCanonical()) {
				assembledContigs++;
				assembledKmer += branch.size();
				assembleContig(seqCollection, fileWriter, branch,
						m_numAssembled + currContigID++);
			}
			m_activeBranchGroups.erase(it++);
		} else
			++it;
	}
	return make_pair(assembledContigs, assembledKmer);
}

/** Send a request for the edges of vertex kmer. */
void NetworkSequenceCollection::generateExtensionRequest(
		uint64_t groupID, uint64_t branchID, const Kmer& kmer)
{
	if (isLocal(kmer)) {
		ExtensionRecord extRec;
		int multiplicity = -1;
		bool success = m_data.getSeqData(kmer, extRec, multiplicity);
		assert(success);
		(void)success;
		processSequenceExtension(groupID, branchID,
				kmer, extRec, multiplicity);
	} else
		m_comm.sendSeqDataRequest(computeNodeID(kmer),
				groupID, branchID, kmer);
}

/** Generate an extension request for each branch of this group. */
void NetworkSequenceCollection::generateExtensionRequests(
		uint64_t groupID,
		BranchGroup::const_iterator first,
		BranchGroup::const_iterator last)
{
	assert(first != last);
#if !NDEBUG
	unsigned length = first->size();
#endif
	unsigned branchID = 0;
	for (BranchGroup::const_iterator it = first; it != last; ++it) {
		assert(it->size() == length);
		generateExtensionRequest(groupID, branchID++,
				it->back().first);
	}
}

void NetworkSequenceCollection::processSequenceExtension(
		uint64_t groupID, uint64_t branchID, const Kmer& seq,
		const ExtensionRecord& extRec, int multiplicity)
{
	switch(m_state)
	{
		case NAS_TRIM:
			return processLinearSequenceExtension(groupID, branchID,
					seq, extRec, multiplicity, m_trimStep);
		case NAS_ASSEMBLE:
		case NAS_COVERAGE:
			return processLinearSequenceExtension(groupID, branchID,
					seq, extRec, multiplicity, UINT_MAX);
		case NAS_DISCOVER_BUBBLES:
			return processSequenceExtensionPop(groupID, branchID,
					seq, extRec, multiplicity,
					opt::bubbleLen - opt::kmerSize + 1);
		case NAS_WAITING:
			if(m_finishedGroups.find(groupID) == m_finishedGroups.end())
			{
				cerr << "Unexpected sequence extension message! gid: " << groupID << " bid: " << branchID << " seq: " << seq.decode() << " Aborting...\n";
				assert(false);
			}
			break;
		default:
			cerr << "Unexpected sequence extension message! State: " << m_state << " gid: " << groupID << " bid: " << branchID << " seq: " << seq.decode() << " Aborting...\n";
			assert(false);
			break;
	}
}

/** Process a sequence extension for trimming. */
void NetworkSequenceCollection::processLinearSequenceExtension(
		uint64_t groupID, uint64_t branchID, const Kmer& seq,
		const ExtensionRecord& extRec, int multiplicity,
		unsigned maxLength)
{
	BranchGroupMap::iterator iter = m_activeBranchGroups.find(groupID);
	assert(iter != m_activeBranchGroups.end());
	Kmer currSeq = seq;
	bool active = AssemblyAlgorithms::processLinearExtensionForBranch(
			iter->second[branchID], currSeq, extRec, multiplicity,
			maxLength);
	if (active)
		generateExtensionRequest(groupID, branchID, currSeq);
}

/** Process a sequence extension for popping. */
void NetworkSequenceCollection::processSequenceExtensionPop(
		uint64_t groupID, uint64_t branchID, const Kmer& seq,
		const ExtensionRecord& extRec, int multiplicity,
		unsigned maxLength)
{
	BranchGroupMap::iterator groupIt
		= m_activeBranchGroups.find(groupID);
	if (groupIt == m_activeBranchGroups.end()) {
		// This branch is already complete. Double check that that is
		// the case.
		assert(m_finishedGroups.count(groupID) > 0);
		return;
	}

	BranchGroup& group = groupIt->second;
	bool extendable = AssemblyAlgorithms::processBranchGroupExtension(
			group, branchID, seq, extRec, multiplicity, maxLength);
	if (extendable && group.updateStatus(maxLength) == BGS_ACTIVE)
		generateExtensionRequests(groupID,
				group.begin(), group.end());
}

/** Add a k-mer to this collection. */
void NetworkSequenceCollection::add(const Kmer& seq)
{
	if (isLocal(seq))
		m_data.add(seq);
	else
		m_comm.sendSeqAddMessage(computeNodeID(seq), seq);
}

/** Remove a k-mer from this collection. */
void NetworkSequenceCollection::remove(const Kmer& seq)
{
	if (isLocal(seq))
		m_data.remove(seq);
	else
		m_comm.sendSeqRemoveMessage(computeNodeID(seq), seq);
}

bool NetworkSequenceCollection::checkpointReached() const
{
	return m_numReachedCheckpoint == opt::numProc;
}

bool NetworkSequenceCollection::checkpointReached(int numRequired) const
{
	return m_numReachedCheckpoint == numRequired;
}

void NetworkSequenceCollection::setFlag(const Kmer& seq, SeqFlag flag)
{
	if (isLocal(seq))
		m_data.setFlag(seq, flag);
	else
		m_comm.sendSetFlagMessage(computeNodeID(seq), seq, flag);
}

bool NetworkSequenceCollection::setBaseExtension(
		const Kmer& seq, extDirection dir, uint8_t base)
{
	if (isLocal(seq)) {
		if (m_data.setBaseExtension(seq, dir, base))
			m_numBasesAdjSet++;
	} else {
		int nodeID = computeNodeID(seq);
		m_comm.sendSetBaseExtension(nodeID, seq, dir, base);
	}

	// As this call delegates, the return value is meaningless so return false
	return false;
}

/** Remove the specified extensions from this k-mer. */
void NetworkSequenceCollection::removeExtension(
		const Kmer& seq, extDirection dir, SeqExt ext)
{
	if (isLocal(seq)) {
		m_data.removeExtension(seq, dir, ext);
		notify(seq);
	} else {
		int nodeID = computeNodeID(seq);
		m_comm.sendRemoveExtension(nodeID, seq, dir, ext);
	}
}

/** Return whether this sequence belongs to this process. */
bool NetworkSequenceCollection::isLocal(const Kmer& seq) const
{
	return computeNodeID(seq) == opt::rank;
}

/** Return the process ID to which the specified kmer belongs. */
int NetworkSequenceCollection::computeNodeID(const Kmer& seq) const
{
	return seq.getCode() % (unsigned)opt::numProc;
}
