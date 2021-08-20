#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "ProgramSlice.h"
#include "FindLazyfiable.h"

namespace llvm {
struct WyvernLazyficationPass : public ModulePass {
	static char ID;
	WyvernLazyficationPass() : ModulePass(ID) {}
	void lazifyCallsite(CallInst &CI, int index, Module &M);

	bool runOnModule(Module&);
	void getAnalysisUsage(AnalysisUsage&) const;
};
}
