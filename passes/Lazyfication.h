#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/ADT/Statistic.h"

#include "FindLazyfiable.h"
#include "ProgramSlice.h"

namespace llvm {
struct WyvernLazyficationPass : public ModulePass {
  static char ID;
  WyvernLazyficationPass() : ModulePass(ID) {}
  bool lazifyCallsite(CallInst &CI, int index, Module &M);

  bool runOnModule(Module &);
  void getAnalysisUsage(AnalysisUsage &) const;
  std::set<std::pair<Function*, Instruction*>> lazifiedFunctions;
};
} // namespace llvm
