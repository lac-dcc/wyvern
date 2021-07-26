#include <set>
#include <stack>
#include <utility>

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"

#include "ProgramDependencyGraph.hh"


namespace llvm {
class ProgramSlice {
	public:
		ProgramSlice(Instruction &I, pdg::ProgramGraph *g);
		Function *outline();
	
	private:
		void addReturnValue(Function *F);
		void reorganizeUses(Function *F);
		void populateBBsWithInsts(Function *F);
		void populateFunctionWithBBs(Function *F);
		void addMissingTerminators(Function *F);
		void insertNewBB(BasicBlock *originalBB, Function *F);

		Instruction* _initial;
		Function* _parentFunction;
		std::set<Instruction*> _instsInSlice;
		std::map<BasicBlock*, BasicBlock*> _origToNewBBmap;
		std::map<BasicBlock*, BasicBlock*> _newToOrigBBmap;
		std::map<Instruction*, Instruction*> _Imap;
};
}

