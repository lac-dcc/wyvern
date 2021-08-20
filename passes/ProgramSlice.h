#include <set>
#include <stack>
#include <utility>

#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"

#include "../PDG/PDGAnalysis.h"

namespace llvm {
class ProgramSlice {
public:
  ProgramSlice(Instruction &I, Function &F);
  bool canOutline();
  SmallVector<Value *> getOrigFunctionArgs();
  Function *outline();

private:
  void reorderBlocks(Function *F);
  void addReturnValue(Function *F);
  void reorganizeUses(Function *F);
  void populateBBsWithInsts(Function *F);
  void populateFunctionWithBBs(Function *F);
  void addMissingTerminators(Function *F);
  void insertNewBB(BasicBlock *originalBB, Function *F);
  SmallVector<Type *> getInputArgTypes();

  Instruction *_initial;
  Function *_parentFunction;
  SmallVector<Argument *> _depArgs;
  std::set<Instruction *> _instsInSlice;

  std::map<Argument *, Argument *> _argMap;
  std::map<BasicBlock *, BasicBlock *> _origToNewBBmap;
  std::map<BasicBlock *, BasicBlock *> _newToOrigBBmap;
  std::map<Instruction *, Instruction *> _Imap;
};
} // namespace llvm
