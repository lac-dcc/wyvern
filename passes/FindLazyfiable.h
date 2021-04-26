#include <set>
#include <stack>
#include <utility>

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"


namespace llvm {
struct FindLazyfiableAnalysis : public ModulePass {
	static char ID;
	FindLazyfiableAnalysis() : ModulePass(ID) {}

	std::set<Function*> lazyFunctions;
	std::set<std::pair<Function*, int>> lazyPaths;
	std::map<std::pair<Function*, int>, int> lazyfiableCallSites;

	bool runOnModule(Module&);
	void DFS(BasicBlock*, BasicBlock*, std::set<BasicBlock*>&, Value*, int);
	void findLazyfiablePaths(Function&);
	bool isArgumentComplex(Instruction*, std::set<Instruction*>, int);
	void analyzeCall(CallInst*);
	void dump_results();
};
}

