#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Local.h"

#include "FindLazyfiable.h"
#include "Lazyfication.h"
#include "ProgramSlice.h"

#include <random>

#define DEBUG_TYPE "WyvernLazyficationPass"

STATISTIC(NumCallsitesLazified,
          "The number of callsites whose arguments were lazified.");
STATISTIC(NumFunctionsLazified,
          "The number of {function, argument} pairs that were lazified.");
STATISTIC(LargestSliceSize,
          "Size of largest slice generated for lazification.");
STATISTIC(SmallestSliceSize,
          "Size of smallest slice generated for lazification.");
STATISTIC(TotalSliceSize,
          "Cumulative size of all slices generated for lazification.");

using namespace llvm;

static cl::opt<bool> WyvernLazificationMemoization(
    "wylazy-memo", cl::init(true),
    cl::desc(
        "Wyvern - Enable memoization in Lazyfication (implement call-by-need"
        "rather than call-by-name)."));
static cl::opt<bool> WyvernLazyficationDebuggingInstrumentation(
    "wylazy-debug", cl::init(false),
    cl::desc("Wyvern - Enable debugging in Lazyfication. This inserts calls"
             "to printf that track when slices are executed."
             "CURRENTLY UNIMPLEMENTED!"));

static unsigned int getNumberOfInsts(Function &F) {
  unsigned int size = 0;
  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      ++size;
    }
  }
  return size;
}

void removeAttributesFromThunkArgument(Value &V, unsigned int index) {
  AttributeMask toRemove;

  toRemove.addAttribute(Attribute::SExt);
  toRemove.addAttribute(Attribute::ZExt);
  toRemove.addAttribute(Attribute::NoAlias);
  toRemove.addAttribute(Attribute::ByRef);
  toRemove.addAttribute(Attribute::NoAlias);
  toRemove.addAttribute(Attribute::NoCapture);
  toRemove.addAttribute(Attribute::ByVal);
  toRemove.addAttribute(Attribute::ReadOnly);
  toRemove.addAttribute(Attribute::WriteOnly);

  if (CallInst *CI = dyn_cast<CallInst>(&V)) {
    CI->removeParamAttrs(index, toRemove);
  } else if (Function *F = dyn_cast<Function>(&V)) {
    F->removeParamAttrs(index, toRemove);
  }
}

void updateThunkArgUses(Function *F, Argument *optimizedArg,
                        Function *slicedFunction) {
  std::map<User *, CallInst *> userCalls;
  std::set<Use *> usesToChange;

  IRBuilder<> builder(F->getContext());

  for (auto &Use : optimizedArg->uses()) {
    Instruction *UserI = dyn_cast<Instruction>(Use.getUser());
    if (UserI && !userCalls.count(UserI)) {
      // if the use is a PHINode, the use happens at the edge, so we cannot
      // insert the thunk load/call at the PHI's block. Instead, we must insert
      // them at the end of the block which flows into the PHI node
      if (PHINode *PN = dyn_cast<PHINode>(UserI)) {
        BasicBlock *origin = PN->getIncomingBlock(Use);
        builder.SetInsertPoint(origin->getTerminator());
      } else {
        builder.SetInsertPoint(UserI);
      }
      Value *thunkFPtrGEP = builder.CreateStructGEP(
          optimizedArg->getType()->getPointerElementType(), optimizedArg, 0,
          "_wyvern_thunk_fptr_addr");
      Value *thunkFPtrLoad =
          builder.CreateLoad(optimizedArg->getType()
                                 ->getPointerElementType()
                                 ->getStructElementType(0),
                             thunkFPtrGEP, "_wyvern_thunkfptr");
      CallInst *thunkCall =
          builder.CreateCall(slicedFunction->getFunctionType(), thunkFPtrLoad,
                             {optimizedArg}, "_wyvern_thunkcall");
      userCalls[UserI] = thunkCall;
    }
  }

  for (auto &Use : optimizedArg->uses()) {
    Instruction *UserI = dyn_cast<Instruction>(Use.getUser());
    if (UserI && userCalls.count(UserI)) {
      usesToChange.insert(&Use);
    }
  }

  for (auto *Use : usesToChange) {
    Use->set(userCalls[Use->getUser()]);
  }
}

Function *cloneCalleeFunction(Function &Callee, int index,
                              Function &slicedFunction, Value *thunkArg,
                              Module &M) {
  SmallVector<Type *> argTypes;
  for (auto &arg : Callee.args()) {
    argTypes.push_back(arg.getType());
  }
  argTypes[index] = thunkArg->getType();

  std::random_device rd;
  std::mt19937 mt(rd());
  std::uniform_int_distribution<int64_t> dist(1, 100000);
  uint64_t random_num = dist(mt);

  FunctionType *FT = FunctionType::get(Callee.getReturnType(), argTypes, false);
  std::string functionName = "_wyvern_calleeclone_" + Callee.getName().str() +
                             "_" + std::to_string(index) +
                             std::to_string(random_num);
  Function *newCallee =
      Function::Create(FT, Function::ExternalLinkage, functionName, M);

  ValueToValueMapTy vMap;
  int idx = -1;
  for (auto &arg : Callee.args()) {
    idx++;
    vMap[&arg] = newCallee->getArg(idx);
    if (idx == index) {
      newCallee->getArg(idx)->setName("_wyvern_thunkptr");
      continue;
    }
    newCallee->getArg(idx)->setName(arg.getName());
  }

  SmallVector<ReturnInst *, 4> Returns;
  CloneFunctionInto(newCallee, &Callee, vMap,
                    CloneFunctionChangeType::LocalChangesOnly, Returns);
  updateThunkArgUses(newCallee, newCallee->getArg(index), &slicedFunction);
  verifyFunction(*newCallee);

  return newCallee;
}

bool WyvernLazyficationPass::lazifyCallsite(CallInst &CI, int index,
                                            Module &M) {
  Instruction *lazyfiableArg;

  if (!(lazyfiableArg = dyn_cast<Instruction>(CI.getArgOperand(index)))) {
    errs() << "Argument is not lazyfiable!\n";
    return false;
  }

  Function *caller = CI.getParent()->getParent();
  ProgramSlice slice = ProgramSlice(*lazyfiableArg, *caller, CI);
  if (!slice.canOutline()) {
    LLVM_DEBUG(dbgs() << "Cannot lazify argument. Slice is not outlineable!\n");
    return false;
  }

  ++NumCallsitesLazified;
  if (lazifiedFunctions.emplace(std::make_pair(caller, lazyfiableArg)).second) {
    ++NumFunctionsLazified;
  }

  Function *callee = CI.getCalledFunction();
  LLVM_DEBUG(dbgs() << "Lazifying: " << *lazyfiableArg << " in func "
                    << caller->getName() << " call to " << callee->getName()
                    << "\n");

  IRBuilder<> builder(M.getContext());
  builder.SetInsertPoint(&CI);

  Function *thunkFunction, *newCallee;
  AllocaInst *thunkAlloca;
  StructType *thunkStructType;
  if (WyvernLazificationMemoization) {
    std::tie(thunkFunction, thunkStructType) = slice.memoizedOutline();

    // allocate thunk, initialize it with:
    // struct thunk {
    //   fptr = thunkFunction
    //   memo_flag = 0
    //   arg0 = x
    //   arg1 = y
    //   ...
    // }
    thunkAlloca =
        builder.CreateAlloca(thunkStructType, nullptr, "_wyvern_thunk_alloca");
    Value *thunkFPtrGEP =
        builder.CreateStructGEP(thunkAlloca->getType()->getPointerElementType(),
                                thunkAlloca, 0, "_wyvern_thunk_fptr_gep");
    builder.CreateStore(thunkFunction, thunkFPtrGEP);
    Value *thunkFlagGEP =
        builder.CreateStructGEP(thunkAlloca->getType()->getPointerElementType(),
                                thunkAlloca, 2, "_wyvern_thunk_flag_gep");
    builder.CreateStore(builder.getInt1(0), thunkFlagGEP);

    unsigned int i = 3;
    for (auto &arg : slice.getOrigFunctionArgs()) {
      Value *thunkArgGEP = builder.CreateStructGEP(
          thunkAlloca->getType()->getPointerElementType(), thunkAlloca, i,
          "_wyvern_thunk_arg_gep_" + arg->getName());
      builder.CreateStore(arg, thunkArgGEP);
      ++i;
    }
  } else {
    std::tie(thunkFunction, thunkStructType) = slice.outline();

    // allocate thunk, initialize it with:
    // struct thunk {
    //   fptr = thunkFunction
    //   arg0 = x
    //   arg1 = y
    //   ...
    // }
    thunkAlloca =
        builder.CreateAlloca(thunkStructType, nullptr, "_wyvern_thunk_alloca");
    Value *thunkFPtrGEP =
        builder.CreateStructGEP(thunkAlloca->getType()->getPointerElementType(),
                                thunkAlloca, 0, "_wyvern_thunk_fptr_gep");
    builder.CreateStore(thunkFunction, thunkFPtrGEP);

    unsigned int i = 1;
    for (auto &arg : slice.getOrigFunctionArgs()) {
      Value *thunkArgGEP = builder.CreateStructGEP(
          thunkAlloca->getType()->getPointerElementType(), thunkAlloca, i,
          "_wyvern_thunk_arg_gep_" + arg->getName());
      builder.CreateStore(arg, thunkArgGEP);
      ++i;
    }
  }

  newCallee =
      cloneCalleeFunction(*callee, index, *thunkFunction, thunkAlloca, M);
  CI.setCalledFunction(newCallee);
  CI.setArgOperand(index, thunkAlloca);
  removeAttributesFromThunkArgument(CI, index);
  removeAttributesFromThunkArgument(*newCallee, index);

  unsigned int sliceSize = getNumberOfInsts(*thunkFunction);
  TotalSliceSize += sliceSize;
  if (LargestSliceSize < sliceSize) {
    LargestSliceSize = sliceSize;
  }
  if (SmallestSliceSize > sliceSize) {
    SmallestSliceSize = sliceSize;
  }

  return true;
}

bool WyvernLazyficationPass::runOnModule(Module &M) {
  SmallestSliceSize = std::numeric_limits<unsigned int>::max();
  FindLazyfiableAnalysis &FLA = getAnalysis<FindLazyfiableAnalysis>();

  bool changed = false;
  for (auto &pair : FLA.getLazyfiableCallSites()) {
    CallInst *callInst = pair.first;
    int argIdx = pair.second;
    Function *callee = pair.first->getCalledFunction();
    if (FLA.getLazyfiablePaths().count(std::make_pair(callee, argIdx)) > 0) {
      changed = lazifyCallsite(*callInst, argIdx, M);
    }
  }

  if (SmallestSliceSize == std::numeric_limits<unsigned int>::max()) {
    SmallestSliceSize = 0;
  }

  return changed;
}

void WyvernLazyficationPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<FindLazyfiableAnalysis>();
  AU.addRequired<LoopInfoWrapperPass>();
  AU.addRequired<CallGraphWrapperPass>();
}

static llvm::RegisterStandardPasses RegisterWyvernLazification(
    llvm::PassManagerBuilder::EP_ModuleOptimizerEarly,
    [](const llvm::PassManagerBuilder &Builder,
       llvm::legacy::PassManagerBase &PM) {
      PM.add(new WyvernLazyficationPass());
    });

char WyvernLazyficationPass::ID = 0;
static RegisterPass<WyvernLazyficationPass>
    X("lazify-callsites",
      "Wyvern - Lazify function arguments for callsites deemed optimizable.",
      false, false);
