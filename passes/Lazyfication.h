#include "llvm/ADT/SmallVector.h"

#include <set>
#include <unordered_map>
#include <utility>

namespace llvm {
struct WyvernCallSiteProfInfo {
  WyvernCallSiteProfInfo(uint8_t numArgs, uint64_t numCalls) {
    _uniqueEvals = SmallVector<int64_t>(numArgs);
    _totalEvals = SmallVector<int64_t>(numArgs);
    _numCalls = numCalls;
  }

  uint64_t _numCalls;
  SmallVector<int64_t> _uniqueEvals;
  SmallVector<int64_t> _totalEvals;
};

struct WyvernLazyficationPass : public ModulePass {
  static char ID;
  WyvernLazyficationPass() : ModulePass(ID) {}

  /**
   * Lazifies the function call @param CI in terms of its actual parameter of
   * index @param index. To do so, the instructions involved in computing the
   * parameter of index @param index are encapsulated in a delegate function
   * generated through program slicing.
   *
   */
  bool lazifyCallsite(CallInst &CI, uint8_t index, Module &M);

  bool shouldLazifyCallsitePGO(CallInst *CI, uint8_t argIdx);

  bool loadProfileInfo(Module &M, std::string path);
  std::set<std::pair<Function *, Instruction *>> lazifiedFunctions;
  std::unordered_map<CallBase *, std::unique_ptr<WyvernCallSiteProfInfo>>
      profileInfo;

  bool runOnModule(Module &);
  void getAnalysisUsage(AnalysisUsage &) const;
};
} // namespace llvm
