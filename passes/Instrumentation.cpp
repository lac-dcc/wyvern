#define DEBUG_TYPE "WyvernInstrumentationPass"

#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
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

static cl::opt<std::string> WyvernInstrumentOutputFile(
    "wyinstr-out-file", cl::init(""),
    cl::desc("Wyvern - Filename for instrumentation output."));

#define DEBUG_TYPE "WyvernInstrumentationPass"

/// When compiling with debug info, LLVM may complain that instructions we add
/// do not have debug locations. This function adds a dummy debug location to
/// the given instruction to silence these errors.
static void updateDebugInfo(Instruction *I, Function *F) {
  if (F->getSubprogram()) {
    I->setDebugLoc(DILocation::get(F->getContext(), 0, 0, F->getSubprogram()));
  }
}

/// Computes unique IDs for each instruction, as offsets from the beginning of
/// the function. Used to identify each instruction uniquely
/// pre-instrumentation, so we can track callsites by their IDs.
static std::map<Instruction *, int64_t> computeInstrIDs(Function *F) {
  std::map<Instruction *, int64_t> instr_ids;
  int64_t instr_id = 0;
  for (inst_iterator I = inst_begin(F); I != inst_end(F); ++I, instr_id++) {
    instr_ids[&*I] = instr_id;
  }

  return instr_ids;
}

void WyvernInstrumentationPass::InstrumentCallSites(
    Function *F, std::map<Instruction *, int64_t> instr_ids,
    std::shared_ptr<std::set<Function *>> promising) {
  inst_iterator I = inst_begin(F);
  for (inst_iterator E = inst_end(F); I != E; ++I) {
    if (CallBase *CB = dyn_cast<CallBase>(&*I)) {

      Function *Callee = CB->getCalledFunction();

      if (Callee && Callee->isDeclaration()) {
        continue;
      }

      if (Callee &&
          (Callee->getName().contains("_wyinstr_") || Callee->isIntrinsic())) {
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
    Function *F, std::map<Instruction *, int64_t> instr_ids,
    std::shared_ptr<std::set<Function *>> promising) {

  for (BasicBlock &BB : *F) {
    for (Instruction &I : BB) {
      if (ReturnInst *RI = dyn_cast<ReturnInst>(&I)) {
        IRBuilder<> builder(RI);
        CallInst *endCall = builder.CreateCall(endCallFun, {});
        updateDebugInfo(endCall, F);
      } else if (CallBase *CB = dyn_cast<CallBase>(&I)) {
        if (CB->getCalledFunction() &&
            CB->getCalledFunction()->getName() == "__cxa_throw") {
          IRBuilder<> builder(CB);
          CallInst *endCall = builder.CreateCall(endCallFun, {});
          updateDebugInfo(endCall, F);
        }
      }
    }
  }

  if (promising && promising->count(F) == 0) {
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
          Constant *binName = builder.CreateGlobalStringPtr(
              WyvernInstrumentOutputFile, "bin_name");
          CallInst *dumpCall = builder.CreateCall(dumpFun, {binName});
          updateDebugInfo(dumpCall, &F);
        }
      }

      if (auto *CI = dyn_cast<CallInst>(&*I)) {
        if (CI->getCalledFunction() &&
            (CI->getCalledFunction()->getName() == "exit" ||
             CI->getCalledFunction()->getName() == "abort")) {
          IRBuilder<> builder(CI);
          Constant *binName = builder.CreateGlobalStringPtr(
              WyvernInstrumentOutputFile, "bin_name");
          CallInst *dumpCall = builder.CreateCall(dumpFun, {binName});
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

  BasicBlock &entryBB = F->getEntryBlock();
  IRBuilder<> builder(&*entryBB.getFirstInsertionPt());
  CallInst *initProfCall = builder.CreateCall(initProfFun, {});
  updateDebugInfo(initProfCall, F);
}

bool WyvernInstrumentationPass::runOnModule(Module &M) {
  if (!WyvernPreInstrument) {
    return false;
  }

  if (WyvernInstrumentOutputFile.empty()) {
    errs() << "Instrumentation output file path not provided!\n";
    return false;
  }

  LLVMContext &Ctx = M.getContext();

  initBitsFun =
      M.getOrInsertFunction("_wyinstr_initbits", Type::getInt64Ty(Ctx));
  markFun =
      M.getOrInsertFunction("_wyinstr_mark_eval", Type::getVoidTy(Ctx),
                            Type::getInt8Ty(Ctx), Type::getInt64PtrTy(Ctx));
  dumpFun = M.getOrInsertFunction("_wyinstr_dump", Type::getVoidTy(Ctx),
                                  Type::getInt8PtrTy(Ctx));
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
        std::make_shared<std::set<Function *>>(FLA.getPromisingFunctions());
  }

  for (auto &F : M) {
    if (F.isDeclaration()) {
      continue;
    }

    F.addFnAttr(Attribute::NoInline);

    std::map<Instruction *, int64_t> instr_ids = computeInstrIDs(&F);
    InstrumentFunction(&F, instr_ids, promisingFunctions);
    InstrumentCallSites(&F, instr_ids, promisingFunctions);
  }

  InstrumentEntryPoint(M);
  InstrumentExitPoints(M);

  return true;
}

void WyvernInstrumentationPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<FindLazyfiableAnalysis>();
  AU.addRequired<TargetLibraryInfoWrapperPass>();
}

static llvm::RegisterStandardPasses RegisterWyvernInstrumentation(
    llvm::PassManagerBuilder::EP_FullLinkTimeOptimizationEarly,
    [](const llvm::PassManagerBuilder &Builder,
       llvm::legacy::PassManagerBase &PM) {
      PM.add(new WyvernInstrumentationPass());
    });

char WyvernInstrumentationPass::ID = 0;
static RegisterPass<WyvernInstrumentationPass>
    X("wyinstr-instrument",
      "Wyvern - Instrument functions to track argument usage.", true, true);
