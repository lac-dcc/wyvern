#include <map>
#include <set>

#include "llvm/Analysis/AliasAnalysis.h"

#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"

namespace llvm {

class ProgramSlice {
public:
  /// Creates a backward slice of function F in terms of slice criterion I,
  /// which is passed as a parameter in call CallSite. Optionally, receives the
  /// result of an Alias Analysis in AA to perform memory safety analysis.
  ProgramSlice(Instruction &I, Function &F, CallInst &CallSite, AAResults *AA,
               TargetLibraryInfo &TLI, bool thunkDebugging);

  /// Returns whether the slice can be safely outlined into a delegate function.
  bool canOutline();

  /// Returns the set of arguments of the slice's parent function. Used to
  /// initialize the environment for thunks that use the slice as their delegate
  /// function.
  SmallVector<Value *> getOrigFunctionArgs();

  /// Returns the struct type of the slice's corresponding thunk used for
  /// lazification.
  StructType *getThunkStructType(bool memo = false);

  /// Returns the delegate function resulted from outlining the slice.
  Function *outline();

  /// Returns the delegate function resulted from outlining the slice, using
  /// memoization.
  Function *memoizedOutline();

private:
  void insertLoadForThunkParams(Function *F, bool memo);
  void printFunctions(Function *F);
  void reorderBlocks(Function *F);
  void rerouteBranches(Function *F);
  ReturnInst *addReturnValue(Function *F);
  void reorganizeUses(Function *F);
  void populateBBsWithInsts(Function *F);
  void populateFunctionWithBBs(Function *F);
  void addMissingTerminators(Function *F);
  void addMemoizationCode(Function *F, ReturnInst *new_ret);
  void insertNewBB(const BasicBlock *originalBB, Function *F);
  void printSlice();
  void computeAttractorBlocks();
  void addDomBranches(DomTreeNode *cur, DomTreeNode *parent,
                      std::set<DomTreeNode *> &visited);
  StructType *computeStructType(bool memo);

  /// pointer to the Instruction used as slice criterion
  Instruction *_initial;

  /// function being sliced
  Function *_parentFunction;

  /// list of formal arguments on which the slice depends on (if any)
  SmallVector<Argument *> _depArgs;

  /// set of instructions that must be in the slice, accordingto dependence
  /// analysis
  std::set<const Instruction *> _instsInSlice;

  /// set of BasicBLocks that must be in the slice, according to dependence
  /// analysis
  std::set<const BasicBlock *> _BBsInSlice;

  /// function call being lazified
  CallInst *_CallSite;

  // @_Imap ->
  /// maps each BasicBlock to its attractor (its first  dominator), used for
  /// rearranging control flow
  std::map<const BasicBlock *, const BasicBlock *> _attractors;

  /// maps original function arguments to new counterparts in the slice function
  std::map<Argument *, Value *> _argMap;

  /// maps BasicBlocks in the original function to their new cloned counterparts
  /// in the slice
  std::map<const BasicBlock *, BasicBlock *> _origToNewBBmap;

  /// same as above, but in the opposite direction
  std::map<BasicBlock *, const BasicBlock *> _newToOrigBBmap;

  /// maps Instructions in the original function to their cloned counterparts in
  /// the slice
  std::map<Instruction *, Instruction *> _Imap;

  /// We store the slice's thunk types, because LLVM does not cache types based
  /// on structure
  StructType *_thunkStructType;
  StructType *_memoizedThunkStructType;

  /// Alias analysis used to evaluate slice safety
  AAResults *_AA;

  /// TargetLibraryInfo for the function, used to detect standard library
  /// functions
  TargetLibraryInfo &_TLI;

  bool _thunkDebugging;
};
} // namespace llvm
