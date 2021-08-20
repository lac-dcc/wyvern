#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

#include "FindLazyfiable.h"

namespace llvm {
struct WyvernInstrumentationPass : public ModulePass {
  static char ID;
  WyvernInstrumentationPass() : ModulePass(ID) {}

  FunctionCallee initBitsFun;
  FunctionCallee markFun;
  FunctionCallee dumpFun;
  FunctionCallee logFun;

  void InstrumentExitPoints(Module &M, Value *num_funcs_arg);
  void InstrumentExit(Function *F, long long func_id, AllocaInst *bits);
  void InstrumentFunction(Function *F, long long func_id);
  AllocaInst *InstrumentEntry(Function *F);
  void getAnalysisUsage(AnalysisUsage &AU) const;
  bool runOnModule(Module &);
};
} // namespace llvm
