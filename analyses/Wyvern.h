#ifndef WYVERN_H_
#define WYVERN_H_

#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/Analysis/PostDominators.h"

class Wyvern : public llvm::FunctionPass {
public:
	static char ID;

	Wyvern();
	bool runOnFunction(llvm::Function &F) override;

	void getAnalysisUsage(llvm::AnalysisUsage &AU) const override;
};

#endif
