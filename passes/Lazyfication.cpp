#define DEBUG_TYPE "WyvernLazyficationPass"

#include "llvm/IR/Verifier.h"

#include "Lazyfication.h"

STATISTIC(NumCallsitesLazified, "The number of callsites whose arguments were lazified.");
STATISTIC(NumFunctionsLazified, "The number of {function, argument} pairs that were lazified.");
STATISTIC(LargestSliceSize, "Size of largest slice generated for lazification.");
STATISTIC(SmallestSliceSize, "Size of smallest slice generated for lazification.");
STATISTIC(TotalSliceSize, "Cumulative size of all slices generated for lazification.");

using namespace llvm;

static cl::opt<bool> WyvernLazificationMemoization(
    "wylazy-memo", cl::init(true),
    cl::desc(
        "Wyvern - Enable memoization in Lazyfication (implement call-by-need"
        "rather than call-by-name)."));

static unsigned int getNumberOfInsts(Function &F) {
  unsigned int size = 0;
  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      ++size;
    }
  }
  return size;
}

void updateThunkArgUses(Function *F, Argument *optimizedArg, Function *slicedFunction) {
  std::map<User*, CallInst*> userCalls;
  std::set<Use*> usesToChange;

  IRBuilder<> builder(F->getContext());

  for (auto *User : optimizedArg->users()) {
    Instruction *UserI = dyn_cast<Instruction>(User);
    if (UserI && !userCalls.count(UserI)) {
      builder.SetInsertPoint(UserI);
      Value *thunkFPtrGEP = builder.CreateStructGEP(optimizedArg, 0, "_wyvern_thunk_fptr_addr");
      Value *thunkFPtrLoad = builder.CreateLoad(thunkFPtrGEP, "_wyvern_thunkfptr");
      CallInst *thunkCall = builder.CreateCall(slicedFunction->getFunctionType(), thunkFPtrLoad, {optimizedArg}, "_wyvern_thunkcall");
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

void updateMemoizedThunkArgUses(Function *F, Value *thunkArg, FunctionType *thunkArgType) {
  LLVMContext &Ctx = F->getParent()->getContext();

  ConstantInt *i32_zero = ConstantInt::get(Type::getInt32Ty(Ctx), 0);

  for (auto &Use : thunkArg->uses()) {
    unsigned int opNo = Use.getOperandNo();
    auto *UserI = dyn_cast<Instruction>(Use.getUser());
    if (UserI) {
      GetElementPtrInst *fPtrGep = GetElementPtrInst::CreateInBounds(thunkArg, { i32_zero, i32_zero }, "_thunk_fptr_addr", UserI);
      LoadInst *fPtrLoad = new LoadInst(fPtrGep->getResultElementType(), fPtrGep, "_thunk_fptr", UserI);
      CallInst *thunkFPtrCall = CallInst::Create(thunkArgType, fPtrLoad, { thunkArg }, "_thunk_call", UserI);
    }
  }
}

Function *cloneCalleeFunction(Function &Callee, int index,
                              Function &slicedFunction, Value *thunkArg, Module &M) {
  SmallVector<Type *> argTypes;
  for (auto &arg : Callee.args()) {
    argTypes.push_back(arg.getType());
  }
  argTypes[index] = thunkArg->getType();

  FunctionType *FT = FunctionType::get(Callee.getReturnType(), argTypes, false);
  std::string functionName = "_wyvern_calleeclone_" + Callee.getName().str() +
                             "_" + std::to_string(index);
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

  if (WyvernLazificationMemoization) {
    updateMemoizedThunkArgUses(newCallee, thunkArg, slicedFunction.getFunctionType());
  } else {
    updateThunkArgUses(newCallee, newCallee->getArg(index), &slicedFunction);
  }

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
  if (!slice.canOutline() || !slice.verify()) {
    errs() << "Cannot lazify argument. Slice is not outlineable!\n";
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

  Function *thunkFunction;
  PointerType *thunkStructPtrType;
  if (WyvernLazificationMemoization) {
    thunkFunction = slice.memoizedOutline();
    
    ConstantInt *i8_zero = ConstantInt::get(Type::getInt8Ty(M.getContext()), 0);
    ConstantInt *i32_zero = ConstantInt::get(Type::getInt32Ty(M.getContext()), 0);
    ConstantInt *i32_two = ConstantInt::get(Type::getInt32Ty(M.getContext()), 2);

    Type *thunkType = ((PointerType*) thunkFunction->arg_begin()->getType())->getElementType();
    AllocaInst *thunkAlloca = new AllocaInst(thunkType, 0, "_thunk_alloca", &CI);
    GetElementPtrInst *thunkFPtrGep = GetElementPtrInst::CreateInBounds(thunkAlloca, { i32_zero }, "_thunk_fptr_gep", &CI);
    StoreInst *thunkFPtrStore = new StoreInst(thunkFunction, thunkFPtrGep, &CI);
    GetElementPtrInst *thunkFlagGep = GetElementPtrInst::CreateInBounds(thunkAlloca, { i32_two }, "_thunk_flag_gep", &CI);
    StoreInst *thunkFlagStore = new StoreInst(i8_zero, thunkFlagGep, &CI);

    Function *newCallee = cloneCalleeFunction(*callee, index, *thunkFunction, thunkAlloca, M);

    CI.setCalledFunction(newCallee);
    CI.setArgOperand(index, thunkAlloca);
  } else {
    std::tie(thunkFunction, thunkStructPtrType) = slice.outline();

    AllocaInst *thunkAlloca = builder.CreateAlloca(thunkStructPtrType->getElementType(), nullptr, "_wyvern_thunk_alloca");
    Value *thunkFPtrGEP = builder.CreateStructGEP(thunkAlloca, 0, "_wyvern_thunk_fptr_gep");
    builder.CreateStore(thunkFunction, thunkFPtrGEP);

    unsigned int i = 1;
    for (auto &arg : slice.getOrigFunctionArgs()) {
      Value *thunkArgGEP = builder.CreateStructGEP(thunkAlloca, i, "_wyvern_thunk_arg_gep_" + arg->getName());
      builder.CreateStore(arg, thunkArgGEP);
      ++i;
    }

    Function *newCallee =
        cloneCalleeFunction(*callee, index, *thunkFunction, thunkAlloca, M);
    CI.setCalledFunction(newCallee);
    CI.setArgOperand(index, thunkAlloca);
  }

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
}

char WyvernLazyficationPass::ID = 0;
static RegisterPass<WyvernLazyficationPass>
    X("lazify-callsites",
      "Wyvern - Lazify function arguments for callsites deemed optimizable.",
      false, false);
