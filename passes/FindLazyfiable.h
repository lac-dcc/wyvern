#include <set>
#include <stack>
#include <unordered_set>
#include <utility>

#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
struct FindLazyfiableAnalysis : public ModulePass {
public:
  static char ID;
  FindLazyfiableAnalysis() : ModulePass(ID) {}

  bool runOnModule(Module &);
  void getAnalysisUsage(AnalysisUsage &) const;

  const std::set<Function *> &getLazyFunctionStats() {
    return _lazyFunctionsStats;
  }
  const std::set<std::pair<Function *, int>> &getLazyPathsStats() {
    return _lazyPathsStats;
  }
  const std::map<std::pair<Function *, int>, int> &
  getLazyfiableCallSiteStats() {
    return _lazyfiableCallSitesStats;
  }

  const std::set<std::pair<Function *, int>> &getLazyfiablePaths() {
    return _lazyfiablePaths;
  }
  const std::set<std::pair<CallInst *, int>> &getLazyfiableCallSites() {
    return _lazyfiableCallSites;
  }

private:
  std::set<Function *> _lazyFunctionsStats;
  std::set<std::pair<Function *, int>> _lazyPathsStats;
  std::map<std::pair<Function *, int>, int> _lazyfiableCallSitesStats;

  std::set<std::pair<Function *, int>> _lazyfiablePaths;
  std::set<std::pair<CallInst *, int>> _lazyfiableCallSites;

  std::set<Function *> addMissingUses(Module &M, LLVMContext &Ctx);
  void DFS(BasicBlock *, BasicBlock *, std::set<BasicBlock *> &, Value *, int);
  void findLazyfiablePaths(Function &);
  bool isArgumentComplex(Instruction &);
  void analyzeCall(CallInst *);
  void dump_results();
};
} // namespace llvm
