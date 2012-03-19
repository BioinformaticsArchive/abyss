#ifndef BRANCHGROUP_H
#define BRANCHGROUP_H 1

#include "BranchRecord.h"
#include "ISequenceCollection.h"
#include <map>

enum BranchGroupStatus 
{
	BGS_ACTIVE,
	BGS_NOEXT,
	BGS_JOINED,
	BGS_TOOLONG,
	BGS_LOOPFOUND,
	BGS_TOOMANYBRANCHES
};

class BranchGroup
{
	public:
		typedef std::vector<BranchRecord> BranchGroupData;
		typedef BranchGroupData::iterator iterator;
		typedef BranchGroupData::const_iterator const_iterator;

		BranchGroup()
			: m_id(0), m_dir(SENSE), m_maxNumBranches(0),
			m_noExt(false), m_status(BGS_ACTIVE)
			{ }

		BranchGroup(uint64_t id, extDirection dir, size_t
				maxNumBranches, const Kmer &origin)
			: m_id(id), m_dir(dir), m_origin(origin),
			m_maxNumBranches(maxNumBranches), m_noExt(false),
			m_status(BGS_ACTIVE) { }

		/** Add a branch to this group. */
		BranchRecord& addBranch(const BranchRecord& branch)
		{
			assert(m_branches.size() < m_maxNumBranches);
			m_branches.push_back(branch);
			return m_branches.back();
		}

		/** Add a branch to this group and extend the new branch with
		 * the given k-mer. */
		void addBranch(const BranchRecord& branch,
				const Kmer& kmer)
		{
			if (m_branches.size() < m_maxNumBranches)
				addBranch(branch).addSequence(kmer);
			else
				m_status = BGS_TOOMANYBRANCHES;
		}

		/** Return the specified branch. */
		BranchRecord& getBranch(unsigned id)
		{
			return m_branches[id];
		}

		/** Return the number of branches in this group. */
		size_t getNumBranches() const { return m_branches.size(); }

		/** Return whether a branch contains the specified k-mer. */
		bool exists(const Kmer& kmer)
		{
			for (BranchGroupData::const_iterator it
					= m_branches.begin();
					it != m_branches.end(); ++it)
				if (it->exists(kmer))
					return true;
			return false;
		}

		// Check the stop conditions for the branch growth
		BranchGroupStatus updateStatus();
		
		// return the current status of the branch
		BranchGroupStatus getStatus() const { return m_status; }
		
		// set the no extension flag
		void setNoExtension() { m_noExt = true; }
		
		// check if the group is active
		bool isActive() const;
		
		// is the no extension flag set?
		bool isNoExt() const { return m_noExt; }
		
		// check if the group is ready for another round of extension (all the branches are the same length)
		bool isExtendable();
		
		// return the direction of growth
		extDirection getDirection() const { return m_dir; }

		iterator begin() { return m_branches.begin(); }
		iterator end() { return m_branches.end(); }

		bool isAmbiguous(const ISequenceCollection* c) const;

	private:
		void sortByCoverage();
		
		BranchGroupData m_branches;
		uint64_t m_id;
		extDirection m_dir;
 		Kmer m_origin;
		size_t m_maxNumBranches;
		bool m_noExt;
		BranchGroupStatus m_status;
};

#endif
