#include "Lazyfication.h"

#define DEBUG_TYPE "WyvernLazyficationPass"

using namespace llvm;

Function *cloneCalleeFunction(Function &Callee, int index, Function &thunk,
                              PointerType *fPtrType, Module &M) {
  SmallVector<Type *> argTypes;
  for (auto &arg : Callee.args()) {
    argTypes.push_back(arg.getType());
  }
  argTypes[index] = fPtrType;

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

  Argument *fPtrArg = newCallee->getArg(index);
  for (auto &Use : fPtrArg->uses()) {
    unsigned int opNo = Use.getOperandNo();
    auto *UserI = dyn_cast<Instruction>(Use.getUser());
    if (UserI) {
      CallInst *thunkCall = CallInst::Create(thunk.getFunctionType(), fPtrArg,
                                             "thunkcall", UserI);
      UserI->setOperand(opNo, thunkCall);
    }
  }

  return newCallee;
}

void WyvernLazyficationPass::lazifyCallsite(CallInst &CI, int index,
                                            Module &M) {
  Instruction *lazyfiableArg;

  if (!(lazyfiableArg = dyn_cast<Instruction>(CI.getArgOperand(index)))) {
    errs() << "Argument is not lazyfiable!\n";
    return;
  }

  Function *caller = CI.getParent()->getParent();
  Function *callee = CI.getCalledFunction();
  ProgramSlice slice = ProgramSlice(*lazyfiableArg, *caller);
  Function *thunk = slice.outline();
  FunctionType *FT = thunk->getFunctionType();
  SmallVector<Value *> args = slice.getOrigFunctionArgs();

  errs() << "Lazifying: " << *lazyfiableArg << " in func " << caller->getName()
         << " call to " << callee->getName() << "\n";

  if (args.size() > 0) {
    errs() << "Cannot lazify slices with input arguments yet!\n";
    return;
  }

  PointerType *functionPtrType = FT->getPointerTo();
  Function *newCallee =
      cloneCalleeFunction(*callee, index, *thunk, functionPtrType, M);
  CI.setCalledFunction(newCallee);
  CI.setArgOperand(index, thunk);
}

bool WyvernLazyficationPass::runOnModule(Module &M) {
  FindLazyfiableAnalysis &FLA = getAnalysis<FindLazyfiableAnalysis>();

  for (auto &pair : FLA.getLazyfiableCallSites()) {
    CallInst *callInst = pair.first;
    int argIdx = pair.second;
    Function *callee = pair.first->getCalledFunction();
    if (FLA.getLazyfiablePaths().count(std::make_pair(callee, argIdx)) > 0) {
      lazifyCallsite(*callInst, argIdx, M);
    }
  }

  return false;
}

void WyvernLazyficationPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<FindLazyfiableAnalysis>();
}

char WyvernLazyficationPass::ID = 0;
static RegisterPass<WyvernLazyficationPass>
    X("lazify-callsites",
      "Wyvern - Lazify function arguments for callsites deemed optimizable.",
      false, false);
