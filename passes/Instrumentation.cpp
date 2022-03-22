#define DEBUG_TYPE "WyvernInstrumentationPass"

#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include "FindLazyfiable.h"
#include "Instrumentation.h"

#include <map>

using namespace llvm;

static cl::opt<bool> WyvernPreInstrument(
    "wyinstr-pre", cl::init(false),
    cl::desc("Wyvern - Determines if instrumentation should be done at all."));

static cl::opt<bool> WyvernInstrumentAll(
    "wyinstr-all", cl::init(false),
    cl::desc("Wyvern - Instrument all functions rather than only those with"
             " paths that do not use an argument."));

#define DEBUG_TYPE "WyvernInstrumentationPass"

static void updateDebugInfo(Instruction *I, Function *F) {
  if (F->getSubprogram()) {
    I->setDebugLoc(DILocation::get(F->getContext(), 0, 0, F->getSubprogram()));
  }
}

static bool shouldInstrumentInvokePath(BasicBlock *targetBB) {
  if (CallInst *CI = dyn_cast<CallInst>(targetBB->getFirstInsertionPt())) {
    if (CI->getCalledFunction() &&
        CI->getCalledFunction()->getName() == "_wyinstr_end_call") {
      return false;
    }
  }

  return true;
}

static std::map<Instruction *, int64_t> computeInstrIDs(Function *F) {
  std::map<Instruction *, int64_t> instr_ids;
  int64_t instr_id = 0;
  for (inst_iterator I = inst_begin(F); I != inst_end(F); ++I, instr_id++) {
    instr_ids[&*I] = instr_id;
  }

  return instr_ids;
}

void WyvernInstrumentationPass::InstrumentCallSites(
    Function *F, const TargetLibraryInfo &TLI,
    std::map<Instruction *, int64_t> instr_ids,
    std::shared_ptr<std::set<Function *>> promising) {
  inst_iterator I = inst_begin(F);
  for (inst_iterator E = inst_end(F); I != E; ++I) {
    if (CallBase *CB = dyn_cast<CallBase>(&*I)) {
      Function *Callee = CB->getCalledFunction();

      if (Callee && (Callee->isIntrinsic() || Callee->getName() == "exit" ||
                     Callee->getName() == "abort")) {
        continue;
      }

      LibFunc libfunc;
      if (Callee && TLI.getLibFunc(Callee->getFunction(), libfunc)) {
        continue;
      }

      else if (promising && promising->count(Callee) == 0) {
        continue;
      }

      else if (Callee && Callee->getName().contains("_wyinstr_")) {
        continue;
      }

      IRBuilder<> builder(CB);
      Constant *callerName =
          builder.CreateGlobalStringPtr(F->getName(), "_wyinstr_caller_name");
      ConstantInt *callInstId = ConstantInt::get(
          builder.getContext(), llvm::APInt(64, instr_ids[CB], true));
      ConstantInt *numArgs = ConstantInt::get(
          builder.getContext(), llvm::APInt(8, CB->arg_size(), true));
      CallInst *initCall =
          builder.CreateCall(initCallFun, {callerName, callInstId, numArgs});
      updateDebugInfo(initCall, F);

      if (CallInst *CI = dyn_cast<CallInst>(CB)) {
        builder.SetInsertPoint(CB->getNextNode());
        CallInst *endCall = builder.CreateCall(endCallFun, {});
        updateDebugInfo(endCall, F);
      }

      // for invokes, instrument both the regular return and the unwind
      // destination
      else if (InvokeInst *II = dyn_cast<InvokeInst>(CB)) {
        // if insertion point already contains a call to _wyinstr_end_call,
        // don't duplicate calls
        if (shouldInstrumentInvokePath(II->getUnwindDest())) {
          builder.SetInsertPoint(II->getUnwindDest(),
                                 II->getUnwindDest()->getFirstInsertionPt());
          CallInst *endCall = builder.CreateCall(endCallFun, {});
          updateDebugInfo(endCall, F);
        }

        if (shouldInstrumentInvokePath(II->getNormalDest())) {
          builder.SetInsertPoint(II->getNormalDest(),
                                 II->getNormalDest()->getFirstInsertionPt());
          CallInst *endCall2 = builder.CreateCall(endCallFun, {});
          updateDebugInfo(endCall2, F);
        }
      }
    }
  }
}

AllocaInst *WyvernInstrumentationPass::InstrumentEntry(Function *F) {
  BasicBlock &entry = F->getEntryBlock();
  LLVMContext &Ctx = F->getParent()->getContext();

  IRBuilder<> builder(&*(entry.getFirstInsertionPt()));
  AllocaInst *alloca = builder.CreateAlloca(builder.getInt64Ty());
  alloca->setName("_wyinstr_bits");
  CallInst *call = builder.CreateCall(initBitsFun);
  call->setName("_wyinstr_call_initbits");
  updateDebugInfo(call, F);
  StoreInst *store = builder.CreateStore(call, alloca);

  return alloca;
}

void WyvernInstrumentationPass::InstrumentFunction(
    Function *F, const TargetLibraryInfo &TLI,
    std::map<Instruction *, int64_t> instr_ids,
    std::shared_ptr<std::set<Function *>> promising) {

  // do not instrument lib functions
  LibFunc libfunc;
  if (TLI.getLibFunc(F->getFunction(), libfunc)) {
    return;
  }

  AllocaInst *usedBits = InstrumentEntry(F);

  std::map<Value *, int> argValues;
  int index = 0;
  for (auto &arg : F->args()) {
    if (Value *vArg = dyn_cast<Value>(&arg)) {
      argValues[vArg] = index;
    }
    ++index;
  }

  inst_iterator I = inst_begin(F);
  for (inst_iterator E = inst_end(F); I != E; ++I) {
    for (Use &U : I->operands()) {
      if (auto *V = dyn_cast<Value>(&U)) {
        // instrument uses of arguments to mark that they were evaluated
        if (argValues.count(V) > 0) {
          IRBuilder<> builder(&*I);

          if (isa<PHINode>(&*I)) {
            BasicBlock *BB = I->getParent();
            builder.SetInsertPoint(BB, BB->getFirstInsertionPt());
          }

          ConstantInt *argIndex = ConstantInt::get(
              F->getParent()->getContext(), llvm::APInt(8, argValues[V], true));
          CallInst *markCall =
              builder.CreateCall(markFun, {argIndex, usedBits});
          updateDebugInfo(markCall, F);
        }
      }
    }
  }
}

void WyvernInstrumentationPass::InstrumentExitPoints(Module &M) {
  LLVMContext &Ctx = M.getContext();

  for (Function &F : M) {
    for (inst_iterator I = inst_begin(F); I != inst_end(F); ++I) {
      if (F.getName() == "main") {
        if (auto *RI = dyn_cast<ReturnInst>(&*I)) {
          IRBuilder<> builder(RI);
          CallInst *dumpCall = builder.CreateCall(dumpFun, {});
          updateDebugInfo(dumpCall, &F);
        }
      }

      if (auto *CI = dyn_cast<CallInst>(&*I)) {
        if (CI->getCalledFunction() &&
            (CI->getCalledFunction()->getName() == "exit" ||
             CI->getCalledFunction()->getName() == "abort")) {
          IRBuilder<> builder(CI);
          CallInst *dumpCall = builder.CreateCall(dumpFun, {});
          updateDebugInfo(dumpCall, &F);
        }
      }
    }
  }
}

void WyvernInstrumentationPass::InstrumentEntryPoint(Module &M) {
  LLVMContext &Ctx = M.getContext();

  Function *F = M.getFunction("main");

  if (!F || F->isDeclaration()) {
    return;
  }

  errs() << "F: " << *F << "\n";
  BasicBlock &entryBB = F->getEntryBlock();
  IRBuilder<> builder(&*entryBB.getFirstInsertionPt());
  CallInst *initProfCall = builder.CreateCall(initProfFun, {});
  updateDebugInfo(initProfCall, F);
}

bool WyvernInstrumentationPass::runOnModule(Module &M) {
  if (!WyvernPreInstrument) {
    return false;
  }

  LLVMContext &Ctx = M.getContext();

  initBitsFun =
      M.getOrInsertFunction("_wyinstr_initbits", Type::getInt64Ty(Ctx));
  markFun =
      M.getOrInsertFunction("_wyinstr_mark_eval", Type::getVoidTy(Ctx),
                            Type::getInt8Ty(Ctx), Type::getInt64PtrTy(Ctx));
  dumpFun = M.getOrInsertFunction("_wyinstr_dump", Type::getVoidTy(Ctx));
  endCallFun = M.getOrInsertFunction("_wyinstr_end_call", Type::getVoidTy(Ctx));
  initCallFun = M.getOrInsertFunction(
      "_wyinstr_init_call", Type::getVoidTy(Ctx), Type::getInt8PtrTy(Ctx),
      Type::getInt64Ty(Ctx), Type::getInt8Ty(Ctx));
  initProfFun =
      M.getOrInsertFunction("_wyinstr_init_prof", Type::getVoidTy(Ctx));

  FindLazyfiableAnalysis &FLA = getAnalysis<FindLazyfiableAnalysis>();

  std::shared_ptr<std::set<Function *>> promisingFunctions = nullptr;
  if (!WyvernInstrumentAll) {
    promisingFunctions =
        std::make_shared<std::set<Function *>>(FLA.getLazyFunctionStats());
  }

  for (auto &F : M) {
    if (F.isDeclaration()) {
      continue;
    }

    const TargetLibraryInfo &TLI =
        getAnalysis<TargetLibraryInfoWrapperPass>().getTLI(F);
    if (promisingFunctions && promisingFunctions->count(&F) == 0) {
      continue;
    }

    std::map<Instruction *, int64_t> instr_ids = computeInstrIDs(&F);
    InstrumentFunction(&F, TLI, instr_ids, promisingFunctions);
    InstrumentCallSites(&F, TLI, instr_ids, promisingFunctions);
  }

  InstrumentEntryPoint(M);
  InstrumentExitPoints(M);

  return true;
}

void WyvernInstrumentationPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<FindLazyfiableAnalysis>();
  AU.addRequired<TargetLibraryInfoWrapperPass>();
}

static llvm::RegisterStandardPasses
    RegisterWyvernLazification(llvm::PassManagerBuilder::EP_EnabledOnOptLevel0,
                               [](const llvm::PassManagerBuilder &Builder,
                                  llvm::legacy::PassManagerBase &PM) {
                                 PM.add(new WyvernInstrumentationPass());
                               });

char WyvernInstrumentationPass::ID = 0;
static RegisterPass<WyvernInstrumentationPass>
    X("wyinstr-instrument",
      "Wyvern - Instrument functions to track argument usage.", true, true);
