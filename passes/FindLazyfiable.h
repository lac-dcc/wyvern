#include <map>
#include <set>
#include <stack>
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

  /**
   * Traverses the module @param M, adding explicit uses of
   * values which are used in PHINodes. This ensures implicit
   * value uses (which are only "visible" in control flow) are
   * properly tracked when finding lazyfiable paths.
   *
   */
  std::set<Function *> addMissingUses(Module &M, LLVMContext &Ctx);

  /**
   * Performs a Depth-First Search over a function's CFG, attempting
   * to find paths from entry BB @param first to exit BB @param exit
   * which do not go through any use of argument @param arg.
   *
   * If any such path is found, record them in the analysis' results
   * and statistics.
   *
   */
  void DFS(BasicBlock *, BasicBlock *, std::set<BasicBlock *> &, Value *, int);

  /**
   * Searches for lazyfiable paths in function @param F, by
   * checking whether there are paths in its CFG which do not
   * use each of its input arguments.
   *
   */
  void findLazyfiablePaths(Function &);

  /**
   * Placeholder.
   * Eventually, should be a function which uses a heuristic to try and
   * estimate statically whether the computation of value @param I is worth
   * lazifying.
   */
  bool isArgumentComplex(Instruction &);

  /**
   * Analyzes a given function callsite @param CI, to evaluate whether
   * any of its arguments can/should be encapsulated into a lazyfied
   * lambda/sliced function.
   *
   */
  void analyzeCall(CallInst *);

  /**
   * Dumps statistics for number of lazyfiable call sites and
   * lazyfiable function paths found within the module.
   *
   */
  void dump_results();
};
} // namespace llvm
