#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/CFLSteensAliasAnalysis.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Local.h"

#include "DebugUtils.h"
#include "FindLazyfiable.h"
#include "Lazyfication.h"
#include "ProgramSlice.h"

#include <fstream>
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

static cl::opt<bool> WyvernLazyficationMemoization(
    "wylazy-memo", cl::init(true),
    cl::desc(
        "Wyvern - Enable memoization in Lazyfication (implement call-by-need"
        "rather than call-by-name)."));

static cl::opt<bool> WyvernEnablePGO(
    "wylazy-pgo", cl::init(false),
    cl::desc("Wyvern - Enable Profile-Guided Optimization. Requires an "
             "instrumentation-generated profile report for the program."));

static cl::opt<std::string>
    WyvernPGOFilePath("wylazy-pgo-file", cl::init(""),
                      cl::desc("Wyvern - Path to instrumentation output file "
                               "containing profile information for PGO."));

static cl::opt<double> WyvernPGOThreshold(
    "wylazy-pgo-threshold", cl::init(0.4),
    cl::desc("Wyvern - Argument evaluation percentage threshold below which "
             "callsite should be lazyfied."));

static cl::opt<bool> WyvernLazyfication(
    "wylazy-enable", cl::init(true),
    cl::desc("Wyvern - Controls whether to enable lazyfication at all (used "
             "for comparison against O3 baseline)"));

static cl::opt<bool> WyvernThunkDebugging(
    "wylazy-debug", cl::init(false),
    cl::desc("Wyvern - Controls whether to generate debugging code for thunks. "
             "This will print data on thunk data structures when they're "
             "initialized and evaluated."));

/// Returns number of instructions in Function @param F. Is used to compute the
/// size of delegate functions generated through slicing.
static unsigned int getNumberOfInsts(Function &F) {
  unsigned int size = 0;
  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      ++size;
    }
  }
  return size;
}

/// When lazifying a callsite, there may be actual parameters which originally
/// had associated parameters that change their optimization/implementation
/// semantics, for instance, noalias or byref/byval. Since we replace these
/// arguments by a thunk, these attributes are no longer valid. This function
/// removes them.
static void removeAttributesFromThunkArgument(Value &V, unsigned int index) {
  AttributeMask toRemove;

  toRemove.addAttribute(Attribute::SExt);
  toRemove.addAttribute(Attribute::ZExt);
  toRemove.addAttribute(Attribute::NoAlias);
  toRemove.addAttribute(Attribute::ByRef);
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

/// At this point, Function @param F was subject to transformations to lazify
/// a function call, as either the caller or the callee.
///
/// In regards to the caller, @param thunkValue is the thunk that is allocated
/// within it and then passed onto a lazyfied callee. However, there may be uses
/// of the lazyfied value @param valueToReplace other than the callsite. We
/// replace uses of this value by calls to the thunk, so that we maximize the
/// amount of potential dead code generation for further optimization.
///
/// In regards to the callee, it was lazyfied and one of its arguments is now
/// @param thunkValue. However, uses of the argument within the function still
/// use it as a value rather than a thunk, so we replace these uses by proper
/// loading/invocation of the thunk.
static void updateThunkArgUses(Function *F, Value *thunkValue,
                               StructType *thunkStructType,
                               Function *slicedFunction,
                               Value *valueToReplace = nullptr) {
  // We could be adding thunk uses in either the caller or callee
  bool isCallee = (valueToReplace == nullptr);
  std::map<Use *, CallInst *> useCalls;

  IRBuilder<> builder(F->getContext());

  Value *toReplace = isCallee ? thunkValue : valueToReplace;
  for (auto &Use : toReplace->uses()) {
    Instruction *UserI = dyn_cast<Instruction>(Use.getUser());
    if (UserI) {
      // If the use is a PHINode, the use happens at the edge, so we cannot
      // insert the thunk load/call at the PHI's block. Instead, we must insert
      // them at the end of the block which flows into the PHI node
      BasicBlock *origin = nullptr;
      if (PHINode *PN = dyn_cast<PHINode>(UserI)) {
        origin = PN->getIncomingBlock(Use);
        builder.SetInsertPoint(origin->getTerminator());
      } else {
        builder.SetInsertPoint(UserI);
      }

      Value *thunkCallTarget = slicedFunction;
      // When optimizing the callee, load the function pointer from the thunk
      if (isCallee) {
        Value *thunkFPtrGEP = builder.CreateStructGEP(
            thunkStructType, thunkValue, 0, "_wyvern_thunk_fptr_addr");
        Value *thunkFPtrLoad =
            builder.CreateLoad(thunkStructType->getStructElementType(0),
                               thunkFPtrGEP, "_wyvern_thunkfptr");
        thunkCallTarget = thunkFPtrLoad;
      }

      // For both caller and callee, add call to delegate function (either
      // loaded from the thunk or directly from the value used to initialize it)
      if (WyvernThunkDebugging) {
        std::string dbg_fmt;
        std::vector<Value *> debug_args;
        raw_string_ostream rso(dbg_fmt);

        rso << "== Wyvern Debugging ==\nCalling thunk!\n";
        rso << "\tInvoking function: " << F->getName() << "\n";
        rso << "======================\n";
        generatePrintf(dbg_fmt, debug_args, builder);
      }

      CallInst *thunkCall =
          builder.CreateCall(slicedFunction->getFunctionType(), thunkCallTarget,
                             {thunkValue}, "_wyvern_thunkcall");

      // Replacing uses/users immediately can break use-def chains. Instead,
      // keep track of all uses to be updated.
      useCalls[&Use] = thunkCall;
    }
  }

  // Update uses
  for (auto &entry : useCalls) {
    Use *use = entry.first;
    CallInst *CI = entry.second;
    use->set(CI);
  }
}

/// Clones function @param Callee, replacing its formal parameter of index
/// @param index with thunk @param thunkArg.
static Function *cloneCalleeFunction(Function &Callee, int index,
                                     Function &slicedFunction, Value *thunkArg,
                                     StructType *thunkStructType, Module &M) {
  SmallVector<Type *> argTypes;
  for (auto &arg : Callee.args()) {
    argTypes.push_back(arg.getType());
  }
  argTypes[index] = thunkArg->getType();

  // generate a random number to use as suffix for clone, to avoid naming
  // conflicts
  // NOTE: we cannot use a simple counter that gets incremented on every
  // clone here, because when optimizing per translation unit, the same callee
  // may be cloned across different translation units
  std::random_device rd;
  std::mt19937 mt(rd());
  std::uniform_int_distribution<int64_t> dist(1, 1000000000);
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
  updateThunkArgUses(newCallee, newCallee->getArg(index), thunkStructType,
                     &slicedFunction);
  verifyFunction(*newCallee);

  return newCallee;
}

bool WyvernLazyficationPass::shouldLazifyCallsitePGO(CallInst *CI,
                                                     uint8_t argIdx) {
  WyvernCallSiteProfInfo *prof_info = profileInfo[CI].get();

  if (!prof_info) {
    return false;
  }

  if (prof_info->_uniqueEvals.size() <= argIdx) {
    return false;
  }

  uint64_t numCalls = prof_info->_numCalls;
  uint64_t uniqueEvals = prof_info->_uniqueEvals[argIdx];
  double evalRate = (double)uniqueEvals / (double)numCalls;

  if (evalRate < WyvernPGOThreshold) {
    return true;
  }

  return false;
}

bool WyvernLazyficationPass::loadProfileInfo(Module &M, std::string path) {
  std::string line;
  std::ifstream profileReportFile(path);
  if (!profileReportFile.is_open()) {
    return false;
  }

  std::string callerName;
  uint64_t instID, numCalls, numArgs;
  std::string argEvals;

  profileReportFile.ignore(std::numeric_limits<std::streamsize>::max(),
                           profileReportFile.widen('\n'));

  std::map<std::string,
           std::map<uint64_t, std::unique_ptr<WyvernCallSiteProfInfo>>>
      rawProfInfo;
  std::string parsed_val;
  while (getline(profileReportFile, parsed_val, ',')) {
    callerName = parsed_val;
    getline(profileReportFile, parsed_val, ',');
    instID = stol(parsed_val);
    getline(profileReportFile, parsed_val, ',');
    numCalls = stol(parsed_val);
    getline(profileReportFile, parsed_val, ',');
    numArgs = stol(parsed_val);

    auto newEntry = std::make_unique<WyvernCallSiteProfInfo>(numArgs, numCalls);

    for (int i = 0; i < numArgs; ++i) {
      getline(profileReportFile, parsed_val, ',');
      newEntry->_uniqueEvals[i] = stol(parsed_val);
    }
    for (int i = 0; i < numArgs; ++i) {
      getline(profileReportFile, parsed_val, ',');
      newEntry->_totalEvals[i] = stol(parsed_val);
    }
    profileReportFile.ignore(std::numeric_limits<std::streamsize>::max(),
                             profileReportFile.widen('\n'));

    if (M.getFunction(callerName) == nullptr) {
      continue;
    }

    rawProfInfo[callerName][instID] = std::move(newEntry);
  }

  for (auto &[funName, prof_infos] : rawProfInfo) {
    Function *F = M.getFunction(funName);
    inst_iterator I = inst_begin(F);
    unsigned inst_id = 0;
    for (; I != inst_end(F); ++I, ++inst_id) {
      if (CallBase *CB = dyn_cast<CallBase>(&*I)) {
        if (prof_infos.count(inst_id)) {
          profileInfo[CB] = std::move(prof_infos[inst_id]);
        }
      }
    }
  }

  return true;
}

static void generateThunkInitializationCode(IRBuilder<> &builder,
                                            ProgramSlice &slice,
                                            AllocaInst *thunkAlloca,
                                            Function *delegateFunction,
                                            bool memo) {
  StructType *thunkStructType = slice.getThunkStructType(memo);

  // initialize thunk with:
  // struct thunk {
  //   fptr = delegateFunction
  // }
  Value *thunkFPtrGEP = builder.CreateStructGEP(thunkStructType, thunkAlloca, 0,
                                                "_wyvern_thunk_fptr_gep");
  builder.CreateStore(delegateFunction, thunkFPtrGEP);

  if (memo) {
    // memoized thunks also have their memoization flag:
    // struct thunk {
    //   ...
    //   memo_flag = false
    //   ...
    // }
    Value *thunkFlagGEP = builder.CreateStructGEP(thunkStructType, thunkAlloca,
                                                  2, "_wyvern_thunk_flag_gep");
    builder.CreateStore(builder.getInt1(0), thunkFlagGEP);
  }

  // add initialization of thunk environment:
  // struct thunk {
  //   ...
  //   arg1 = x
  //   arg2 = y
  //   ...
  // }
  uint64_t i = (memo ? 3 : 1);
  for (auto &arg : slice.getOrigFunctionArgs()) {
    Value *thunkArgGEP =
        builder.CreateStructGEP(thunkStructType, thunkAlloca, i,
                                "_wyvern_thunk_arg_gep_" + arg->getName());
    builder.CreateStore(arg, thunkArgGEP);
    ++i;
  }

  if (WyvernThunkDebugging) {
    std::string dbg_fmt;
    std::vector<Value *> debug_args;
    raw_string_ostream rso(dbg_fmt);

    rso << "== Wyvern Debugging ==\nInitializing thunk with:\n";
    rso << "\tdelegateFunction = " << delegateFunction->getName().str() << "\n";

    if (memo) {
      rso << "\t";
      builder.getInt1(0)->getType()->print(rso);
      rso << " memo_flag = ";
      builder.getInt1(0)->print(rso);
      rso << "\n";
    }

    for (auto &arg : slice.getOrigFunctionArgs()) {
      rso << "\t";
      arg->getType()->print(rso);
      rso << " " << arg->getName() << " = ";
      if (arg->getType()->isIntegerTy()) {
        rso << "%d";
        debug_args.push_back(arg);
      } else if (arg->getType()->isFloatingPointTy()) {
        rso << "%f";
        debug_args.push_back(arg);
      } else if (arg->getType()->isPointerTy() && arg->getType() == builder.getInt8PtrTy()) {
        rso << "%s";
        debug_args.push_back(arg);
      }
      rso << "\n";
    }

    rso << "======================\n";
    generatePrintf(rso.str(), debug_args, builder);
  }
}

/// Attempts to lazify a given call site, in terms of its actual parameter with
/// the given index.
bool WyvernLazyficationPass::lazifyCallsite(CallInst &CI, uint8_t index,
                                            Module &M, AAResults *AA) {
  LLVM_DEBUG(dbgs() << "Analyzing callsite: " << CI << " for argument "
                    << *CI.getOperand(index) << "\n");

  Instruction *lazyfiableArg;
  if (!(lazyfiableArg = dyn_cast<Instruction>(CI.getArgOperand(index)))) {
    LLVM_DEBUG(dbgs() << "Argument is not lazyfiable!\n");
    return false;
  }

  Function *caller = CI.getParent()->getParent();
  TargetLibraryInfo &TLI =
      getAnalysis<TargetLibraryInfoWrapperPass>().getTLI(*caller);
  ProgramSlice slice =
      ProgramSlice(*lazyfiableArg, *caller, CI, AA, TLI, WyvernThunkDebugging);

  if (!slice.canOutline()) {
    LLVM_DEBUG(dbgs() << "Cannot lazify argument. Slice is not outlineable!\n");
    return false;
  }

  Function *callee = CI.getCalledFunction();
  if (!callee || callee->isDeclaration()) {
    LLVM_DEBUG(dbgs() << "Cannot lazify argument. Callee function definition "
                         "is not available for cloning!\n");
    return false;
  }

  if (callee->isVarArg()) {
    LLVM_DEBUG(dbgs() << "Cannot lazify argument. Callee function has variable "
                         "number of input arguments!\n");
    return false;
  }

  Argument *callee_arg = callee->getArg(index);
  if (callee_arg->getNumUses() == 0) {
    LLVM_DEBUG(dbgs() << "Will not lazify argument because it has no uses in "
                         "callee function! Possibly @this pointer?\n");
    return false;
  }

  ++NumCallsitesLazified;
  if (lazifiedFunctions.emplace(std::make_pair(caller, lazyfiableArg)).second) {
    ++NumFunctionsLazified;
  }

  LLVM_DEBUG(dbgs() << "Lazifying: " << *lazyfiableArg << " in func "
                    << caller->getName() << " call to " << callee->getName()
                    << "\n");

  IRBuilder<> builder(M.getContext());
  builder.SetInsertPoint(&*(caller->getEntryBlock().getFirstInsertionPt()));

  Function *delegateFunction, *newCallee;
  StructType *thunkStructType;

  delegateFunction =
      WyvernLazyficationMemoization ? slice.memoizedOutline() : slice.outline();
  thunkStructType = slice.getThunkStructType(WyvernLazyficationMemoization);

  AllocaInst *thunkAlloca =
      builder.CreateAlloca(thunkStructType, nullptr, "_wyvern_thunk_alloca");

  if (isa<PHINode>(lazyfiableArg)) {
    builder.SetInsertPoint(
        &*(lazyfiableArg->getParent()->getFirstInsertionPt()));
  } else {
    builder.SetInsertPoint(lazyfiableArg);
  }

  generateThunkInitializationCode(builder, slice, thunkAlloca, delegateFunction,
                                  WyvernLazyficationMemoization);

  auto tuple = std::make_tuple(callee, index, thunkStructType);
  Function *previouslyClonedCallee = clonedCallees[tuple];
  if (previouslyClonedCallee) {
    newCallee = previouslyClonedCallee;
  } else {
    newCallee = cloneCalleeFunction(*callee, index, *delegateFunction,
                                    thunkAlloca, thunkStructType, M);
    clonedCallees[tuple] = newCallee;
  }

  CI.setCalledFunction(newCallee);
  CI.setArgOperand(index, thunkAlloca);
  removeAttributesFromThunkArgument(CI, index);
  removeAttributesFromThunkArgument(*newCallee, index);
  updateThunkArgUses(caller, thunkAlloca, thunkStructType, delegateFunction,
                     lazyfiableArg);

  uint64_t sliceSize = getNumberOfInsts(*delegateFunction);
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

  if (!WyvernLazyfication) {
    return false;
  }

  bool changed = false;
  if (WyvernEnablePGO) {
    if (!loadProfileInfo(M, WyvernPGOFilePath)) {
      errs() << "Failed to load profile info for PGO! Exiting...\n";
      return false;
    }

    for (Function &F : M) {
      for (inst_iterator I = inst_begin(F); I != inst_end(F); ++I) {
        if (!isa<CallInst>(&*I)) {
          continue;
        }
        CallInst *CI = cast<CallInst>(&*I);
        for (uint8_t argIdx = 0; argIdx < CI->arg_size(); ++argIdx) {
          if (shouldLazifyCallsitePGO(CI, argIdx)) {
            AAResults *AA =
                &getAnalysis<AAResultsWrapperPass>(F).getAAResults();
            changed = lazifyCallsite(*CI, argIdx, M, AA);
            if (changed) {
              break;
            }
          }
        }
      }
    }
  }

  else {
    for (auto &pair : FLA.getLazyfiableCallSites()) {
      CallInst *CI = pair.first;
      uint8_t argIdx = pair.second;
      Function *caller = CI->getParent()->getParent();
      Function *callee = pair.first->getCalledFunction();

      AAResults *AA =
          &getAnalysis<AAResultsWrapperPass>(*caller).getAAResults();
      if (FLA.getPromisingFunctionArgs().count(std::make_pair(callee, argIdx)) >
          0) {
        changed = lazifyCallsite(*CI, argIdx, M, AA);
      }
    }
  }

  if (SmallestSliceSize == std::numeric_limits<unsigned int>::max()) {
    SmallestSliceSize = 0;
  }

  return changed;
}

void WyvernLazyficationPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<AssumptionCacheTracker>();
  AU.addRequired<FindLazyfiableAnalysis>();
  AU.addRequired<TargetLibraryInfoWrapperPass>();
  AU.addRequired<LoopInfoWrapperPass>();
  AU.addRequired<CallGraphWrapperPass>();
  AU.addRequired<DominatorTreeWrapperPass>();
  AU.addRequired<BasicAAWrapperPass>();
  AU.addRequired<GlobalsAAWrapperPass>();
  AU.addRequired<CFLSteensAAWrapperPass>();
  AU.addRequired<AAResultsWrapperPass>();
  AU.addRequired<TargetLibraryInfoWrapperPass>();
}

static llvm::RegisterStandardPasses RegisterWyvernLazificationLTO(
    llvm::PassManagerBuilder::EP_FullLinkTimeOptimizationEarly,
    [](const llvm::PassManagerBuilder &Builder,
       llvm::legacy::PassManagerBase &PM) {
      PM.add(new WyvernLazyficationPass());
      // Since we explicitly run LCSSA during our analyses, there may be
      // leftover invalid PHINodes created by it in the program. We must then
      // run -inst-combine explicitly to remove them (as LLVM itself does behind
      // the scenes).
      PM.add(llvm::createInstructionCombiningPass());
    });

static llvm::RegisterStandardPasses RegisterWyvernLazification(
    llvm::PassManagerBuilder::EP_ModuleOptimizerEarly,
    [](const llvm::PassManagerBuilder &Builder,
       llvm::legacy::PassManagerBase &PM) {
      PM.add(new WyvernLazyficationPass());
      // Since we explicitly run LCSSA during our analyses, there may be
      // leftover invalid PHINodes created by it in the program. We must then
      // run -inst-combine explicitly to remove them (as LLVM itself does behind
      // the scenes).
      PM.add(llvm::createInstructionCombiningPass());
    });

char WyvernLazyficationPass::ID = 0;
static RegisterPass<WyvernLazyficationPass>
    X("lazify-callsites",
      "Wyvern - Lazify function arguments for callsites deemed optimizable.",
      false, false);
