#include "FindLazyfiable.h"

#define DEBUG_TYPE "FindLazyfiablePass"

using namespace llvm;

void FindLazyfiableAnalysis::DFS(BasicBlock *first, BasicBlock *exit, std::set<BasicBlock*> &visited, Value *arg, int index) {
	std::stack<BasicBlock*> st;
	st.push(first);
	visited.insert(first);

	while(!st.empty()) {
		BasicBlock* cur = st.top();
		st.pop();
		bool hasUse = false;
		for (Instruction &I : *cur) {
			for (Use &U : I.operands()) {
				if (Value *vUse = dyn_cast<Value>(U)) {
					if (vUse == arg) {
						hasUse = true;
					}
				}
			}
		}

		if (hasUse) {
			continue;
		}

		if (cur == exit) {
			auto pair = std::make_pair(cur->getParent(), index);
			lazyPaths.insert(pair);
			lazyFunctions.insert(cur->getParent());
		}

		for (auto it = succ_begin(cur), it_end = succ_end(cur); it != it_end; ++it) {
			BasicBlock *succ = *it;
			if (visited.count(succ) == 0) {
				visited.insert(succ);
				st.push(succ);
			}
		}
	}
}

void FindLazyfiableAnalysis::findLazyfiablePaths(Function &F) {
	BasicBlock& entry = F.getEntryBlock();
	BasicBlock* exit;

	for (BasicBlock &BB : F) {
		for (Instruction &I : BB) {
			if (auto *RI = dyn_cast<ReturnInst>(&I)) {
				exit = &BB;
			}
		}
	}

	std::set<BasicBlock*> visited;
	std::set<Value*> used;
	unsigned int index = 0;
	for (auto &arg : F.args()) {
		if (Value *vArg = dyn_cast<Value>(&arg)) {
			DFS(&entry, exit, visited, vArg, index);
		}
		++index;
	}
}

bool FindLazyfiableAnalysis::isArgumentComplex(Instruction *I, std::set<Instruction*> analyzed, int depth) {
	if (depth > 12) {
		return false;
	}

	bool ret = false;

	analyzed.insert(I);
	if (CallInst *CI = dyn_cast<CallInst>(I)) {
		return true;
	}

	for (auto &op : I->operands()) {
		if (Instruction *I = dyn_cast<Instruction>(&op)) {
			if (analyzed.count(I) == 0) {
				ret |= isArgumentComplex(I, analyzed, depth+1);
			}
		}
	}

	return ret;
}

void FindLazyfiableAnalysis::analyzeCall(CallInst *CI) {
	Function *Callee = CI->getCalledFunction();
	if (Callee == nullptr || Callee->isDeclaration()) {
		return;
	}

	std::set<Instruction*> analyzed_insts;
	for (auto &arg : CI->args()) {
		if (Instruction *I = dyn_cast<Instruction>(&arg)) {
			unsigned int index = CI->getArgOperandNo(&arg);
			if (isArgumentComplex(I, analyzed_insts, 0)) {
				auto pair = std::make_pair(Callee, index);
				lazyfiableCallSites[pair] += 1;
			}	
		}
	}
}

bool FindLazyfiableAnalysis::runOnModule(Module &M) {
	for (Function &F : M) {
		for (auto I = inst_begin(F), E = inst_end(F); I != E; ++I) {
			if (CallInst *CI = dyn_cast<CallInst>(&*I)) {
				analyzeCall(CI);
			}
		}

		if (F.isDeclaration() || F.isVarArg()) {
			continue;
		}

		findLazyfiablePaths(F);
	}

	dump_results();

	return false;
}

void FindLazyfiableAnalysis::dump_results() {
	std::error_code ec;
	raw_fd_ostream outfile("lazyfiable.csv", ec);
		
	outfile << "function,lazyArg\n";
	for (auto &entry : lazyPaths) {
		outfile << entry.first->getName() << "," << entry.second << "\n";
	}

	outfile << "\n\nfunction,arg,callSites\n";
	for (auto &entry : lazyfiableCallSites) {
		outfile << entry.first.first->getName() << "," << entry.first.second << "," << entry.second << "\n";
	}

	outfile << "\n\nfunctionsInBoth\n";
	for (auto &entry : lazyfiableCallSites) {
		if (lazyPaths.count(entry.first) > 0) {
			outfile << entry.first.first->getName() << "," << entry.first.second << "\n";
		}
	}
}

char FindLazyfiableAnalysis::ID = 0;
static RegisterPass<FindLazyfiableAnalysis> X("find-lazyfiable", "Find Lazyfiable function arguments.", false, false);
