#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"


namespace llvm {
struct WyvernInstrumentationPass : public ModulePass {
	static char ID;
	WyvernInstrumentationPass() : ModulePass(ID) {}

	FunctionCallee initBitsFun;
	FunctionCallee initFun;
	FunctionCallee markFun;
	FunctionCallee dumpFun;
	FunctionCallee logFun;

	void InstrumentExit(Function *F, long long func_id, AllocaInst *bits); 
	void InstrumentFunction(Function *F, long long func_id); 
	AllocaInst* InstrumentEntry(Function *F);
	void getAnalysisUsage(AnalysisUsage &AU) const;
	bool runOnModule(Module&);
};
}

