#include <map>
#include <set>

#include "llvm/Analysis/AliasAnalysis.h"

#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"

namespace llvm {

class ProgramSlice {
public:
  ProgramSlice(Instruction &I, Function &F, CallInst &CallSite, AAResults *AA);
  bool canOutline();
  SmallVector<Value *> getOrigFunctionArgs();
  StructType *getThunkStructType(bool memo = false);
  Function *outline();
  Function *memoizedOutline();
  unsigned int size();

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
  SmallVector<Type *> getInputArgTypes();
  StructType *computeStructType(bool memo);

  // Private data members
  // @_initial -> pointer to the Instruction used as slice criterion
  // @_parentFunction -> function being sliced
  // @_depArgs -> list of formal arguments on which the slice depends on (if
  // any)
  // @_instsInSlice -> set of instructions that must be in the slice, according
  // to dependence analysis
  // @_BBsInSlice -> set of BasicBLocks that must be in the slice, according to
  // dependence analysis
  // @_CallSite -> function call being lazified
  Instruction *_initial;
  Function *_parentFunction;
  SmallVector<Argument *> _depArgs;
  std::set<const Instruction *> _instsInSlice;
  std::set<const BasicBlock *> _BBsInSlice;
  CallInst *_CallSite;

  // @_attractors -> maps each BasicBlock to its attractor (its first
  // dominator), used for rearranging control flow
  // @_argMap -> maps original function arguments to new counterparts in the
  // slice function
  // @_origToNewBBmap -> maps BasicBlocks in the original function to their new
  // cloned counterparts in the slice
  // @_newToOrigBBmap -> same as above, but in the opposite direction
  // @_Imap -> maps Instructions in the original function to their cloned
  // counterparts in the slice
  std::map<const BasicBlock *, const BasicBlock *> _attractors;
  std::map<Argument *, Value *> _argMap;
  std::map<const BasicBlock *, BasicBlock *> _origToNewBBmap;
  std::map<BasicBlock *, const BasicBlock *> _newToOrigBBmap;
  std::map<Instruction *, Instruction *> _Imap;

  // We store the slice's thunk types, because LLVM does not cache types based
  // on structure
  StructType *_thunkStructType;
  StructType *_memoizedThunkStructType;

  // Alias analysis used to evaluate slice safety
  AAResults *_AA;
};
} // namespace llvm
