#include "llvm/ADT/SmallVector.h"

#include <set>
#include <unordered_map>
#include <utility>

namespace llvm {

/// Struct that represents a given instance of profiling information. For each
/// call site, the profile info gives us the number of times the call site was
/// called, the number of times each argument was uniquely evaluated at least
/// once per call, and the total number of evaluations for each argument.
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

  /// Lazifies the function call @param CI in terms of its actual parameter of
  /// index @param index. To do so, the instructions involved in computing the
  /// parameter of index @param index are encapsulated in a delegate function
  /// generated through program slicing.
  bool lazifyCallsite(CallInst &CI, uint8_t index, Module &M, AAResults *AA);

  /// Returns whether a call site + param pair should be lazified, taking into
  /// account the input profiling information.
  bool shouldLazifyCallsitePGO(CallInst *CI, uint8_t argIdx);

  /// Loads profile information from the input profiling report file.
  bool loadProfileInfo(Module &M, std::string path);

  /// Stores the set of callee function + argument pairs that were lazified.
  std::set<std::pair<Function *, Instruction *>> lazifiedFunctions;

  /// Stores the map of function calls to their corresponding profile data.
  std::unordered_map<CallBase *, std::unique_ptr<WyvernCallSiteProfInfo>>
      profileInfo;

  /// Caches the previously cloned callee functions, to be reused if possible.
  std::map<std::tuple<Function *, unsigned, StructType *>, Function *>
      clonedCallees;

  bool runOnModule(Module &);
  void getAnalysisUsage(AnalysisUsage &) const;
};
} // namespace llvm
