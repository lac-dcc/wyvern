#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

namespace llvm {
struct WyvernInstrumentationPass : public ModulePass {
  static char ID;
  WyvernInstrumentationPass() : ModulePass(ID) {}

  FunctionCallee initProfFun;
  FunctionCallee initBitsFun;
  FunctionCallee markFun;
  FunctionCallee dumpFun;
  FunctionCallee initCallFun;
  FunctionCallee endCallFun;

  void InstrumentEntryPoint(Module &M);
  void InstrumentExitPoints(Module &M);
  void
  InstrumentFunction(Function *F, const TargetLibraryInfo &TLI,
                     std::map<Instruction *, int64_t> instr_ids,
                     std::shared_ptr<std::set<Function *>> promising = nullptr);
  void InstrumentCallsite(CallBase *I, Function *F);
  void InstrumentCallSites(
      Function *F, const TargetLibraryInfo &TLI,
      std::map<Instruction *, int64_t> instr_ids,
      std::shared_ptr<std::set<Function *>> promising = nullptr);
  AllocaInst *InstrumentEntry(Function *F);
  void getAnalysisUsage(AnalysisUsage &AU) const;
  bool runOnModule(Module &);
};
} // namespace llvm
