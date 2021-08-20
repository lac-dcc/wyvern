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
			if (isa<PHINode>(I)) {
				continue;
			}
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
			_lazyPathsStats.insert(pair);
			_lazyFunctionsStats.insert(cur->getParent());
			_lazyfiablePaths.insert(std::make_pair(cur->getParent(), index));
			return;
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
	BasicBlock* exit = nullptr;

	for (BasicBlock &BB : F) {
		for (Instruction &I : BB) {
			if (auto *RI = dyn_cast<ReturnInst>(&I)) {
				exit = &BB;
			}
		}
	}

	if (exit == nullptr) {
		return;
	}

	unsigned int index = 0;
	for (auto &arg : F.args()) {
		std::set<BasicBlock*> visited;
		if (Value *vArg = dyn_cast<Value>(&arg)) {
			DFS(&entry, exit, visited, vArg, index);
		}
		++index;
	}
}

bool FindLazyfiableAnalysis::isArgumentComplex(Instruction& I) {	
	return true;
}

void FindLazyfiableAnalysis::analyzeCall(CallInst *CI) {
	Function *Callee = CI->getCalledFunction();
	if (Callee == nullptr || Callee->isDeclaration()) {
		return;
	}

	for (auto &arg : CI->args()) {
		if (Instruction *I = dyn_cast<Instruction>(&arg)) {
			unsigned int index = CI->getArgOperandNo(&arg);
			if (isArgumentComplex(*I)) {
				auto pair = std::make_pair(Callee, index);
				_lazyfiableCallSitesStats[pair] += 1;
				_lazyfiableCallSites.insert(std::make_pair(CI, index));
			}	
		}
	}
}

void removeDummyFunctions(std::set<Function*> dummyFunctions) {
	for (auto &F : dummyFunctions) {
		for (auto User : F->users()) {
			CallInst* dummyFunCall = (CallInst*) User;
			dummyFunCall->eraseFromParent();
		}
		F->eraseFromParent();
	}
}

FunctionCallee getDummyFunctionForType(Module &M, Type* Type) {
	LLVMContext& Ctx = M.getContext();
	std::string typeName;
	raw_string_ostream typeNameOs(typeName);
	Type->print(typeNameOs);
	std::string dummyFunctionName = "dummy_fun" + typeNameOs.str();
	FunctionCallee dummyFunction = M.getOrInsertFunction(dummyFunctionName, Type::getVoidTy(Ctx), Type);
	return dummyFunction;
}

std::set<Function*> FindLazyfiableAnalysis::addMissingUses(Module &M, LLVMContext& Ctx) {
	std::set<Function*> dummyFunctions;
	for (Function &F : M) {
		std::set<Value*> vArgs;
		for (auto &arg : F.args()) {
			if (Value* vArg = dyn_cast<Value>(&arg)) {
				vArgs.insert(vArg);
			}	
		}

		for (auto I = inst_begin(F), E = inst_end(F); I != E; ++I) {
			if (PHINode *PN = dyn_cast<PHINode>(&*I)) {
				for (auto &value : PN->operands()) {
					if (!vArgs.count(value)) {
						continue;
					}

					FunctionCallee dummyFunction = getDummyFunctionForType(M, value->getType());
					dummyFunctions.insert((Function *) dummyFunction.getCallee());
					Value* args[] = { value };
					BasicBlock* incBlock = PN->getIncomingBlock(value);
					IRBuilder<> builder(incBlock, --incBlock->end());
					builder.CreateCall(dummyFunction, args);
				}
			}
		}
	}
	return dummyFunctions;
}

bool FindLazyfiableAnalysis::runOnModule(Module &M) {
	std::set<Function*> dummyFunctions = addMissingUses(M, M.getContext());

	for (Function &F : M) {
		if (F.isDeclaration() || F.isVarArg()) {
			continue;
		}

		findLazyfiablePaths(F);

		for (auto I = inst_begin(F), E = inst_end(F); I != E; ++I) {
			if (CallInst *CI = dyn_cast<CallInst>(&*I)) {
				analyzeCall(CI);
			}
		}
	}

	removeDummyFunctions(dummyFunctions);

	dump_results();

	return false;
}

void FindLazyfiableAnalysis::dump_results() {
	std::error_code ec;
	raw_fd_ostream outfile("lazyfiable.csv", ec);
		
	outfile << "function,lazyArg\n";
	for (auto &entry : _lazyPathsStats) {
		outfile << entry.first->getName() << "," << entry.second << "\n";
	}

	outfile << "\n\nfunction,arg,callSites\n";
	for (auto &entry : _lazyfiableCallSitesStats) {
		outfile << entry.first.first->getName() << "," << entry.first.second << "," << entry.second << "\n";
	}

	outfile << "\n\nfunctionsInBoth\n";
	for (auto &entry : _lazyfiableCallSitesStats) {
		if (_lazyPathsStats.count(entry.first) > 0) {
			outfile << entry.first.first->getName() << "," << entry.first.second << "\n";
		}
	}
}

void FindLazyfiableAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {
}

char FindLazyfiableAnalysis::ID = 0;
static RegisterPass<FindLazyfiableAnalysis> X("find-lazyfiable", "Wyvern - Find Lazyfiable function arguments.", false, false);
