#include <set>

#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h" 
#include "llvm/IR/Dominators.h"

namespace llvm {
  
class ProgramSlice {
public:
  ProgramSlice(Instruction &I, Function &F, CallInst &CallSite);
  bool canOutline();
  SmallVector<Value *> getOrigFunctionArgs();
  std::tuple<Function*, PointerType*> outline();
  std::tuple<Function*, PointerType*> memoizedOutline();
  unsigned int size();

private:
  void insertLoadForThunkParams(Function *F, bool memo);
  bool hasUniqueAttractor(Instruction *terminator);
  const BasicBlock *getUniqueAttractor(Instruction *terminator);
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
  void addDomBranches(DomTreeNode *cur, DomTreeNode *parent, std::set<DomTreeNode*> &visited);
  SmallVector<Type *> getInputArgTypes();

  Instruction *_initial;
  Function *_parentFunction;
  SmallVector<Argument *> _depArgs;
  std::set<const Instruction *> _instsInSlice;
  std::set<const BasicBlock *> _BBsInSlice;
  CallInst *_CallSite;

  std::map<const BasicBlock*, const BasicBlock*> _attractors;
  std::map<Argument*, Value*> _argMap;
  std::map<const BasicBlock *, BasicBlock *> _origToNewBBmap;
  std::map<BasicBlock *, const BasicBlock *> _newToOrigBBmap;
  std::map<Instruction *, Instruction *> _Imap;
};
} // namespace llvm
