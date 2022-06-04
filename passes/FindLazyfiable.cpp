#define DEBUG_TYPE "FindLazyfiablePass"

#include "FindLazyfiable.h"

#include "llvm/Passes/PassBuilder.h"
#include "llvm/Transforms/IPO/FunctionAttrs.h"
#include "llvm/Transforms/Utils/LCSSA.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"
#include "llvm/Transforms/Utils/UnifyFunctionExitNodes.h"

#include <map>

using namespace llvm;

void FindLazyfiableAnalysis::DFS(BasicBlock *first, BasicBlock *exit,
                                 std::set<BasicBlock *> &visited, Value *arg,
                                 int index) {
  std::stack<BasicBlock *> st;
  st.push(first);
  visited.insert(first);

  while (!st.empty()) {
    BasicBlock *cur = st.top();
    st.pop();
    bool hasUse = false;
    for (Instruction &I : *cur) {
      if (isa<PHINode>(I)) {
        continue;
      }
      for (Use &U : I.operands()) {
        if (Value *vUse = dyn_cast<Value>(U)) {
          if (vUse == arg) {
            hasUse = true;
          }
        }
      }
    }

    if (hasUse) {
      continue;
    }

    if (cur == exit) {
      auto pair = std::make_pair(cur->getParent(), index);
      _promisingFunctions.insert(cur->getParent());
      _promisingFunctionArgs.insert(pair);
      return;
    }

    for (auto it = succ_begin(cur), it_end = succ_end(cur); it != it_end;
         ++it) {
      BasicBlock *succ = *it;
      if (visited.count(succ) == 0) {
        visited.insert(succ);
        st.push(succ);
      }
    }
  }
}

void FindLazyfiableAnalysis::findLazyfiablePaths(Function &F) {
  BasicBlock &entry = F.getEntryBlock();
  BasicBlock *exit = nullptr;

  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      if (auto *RI = dyn_cast<ReturnInst>(&I)) {
        exit = &BB;
      }
    }
  }

  if (exit == nullptr) {
    return;
  }

  unsigned int index = 0;
  for (auto &arg : F.args()) {
    std::set<BasicBlock *> visited;
    if (Value *vArg = dyn_cast<Value>(&arg)) {
      DFS(&entry, exit, visited, vArg, index);
    }
    ++index;
  }
}

bool FindLazyfiableAnalysis::isArgumentComplex(Instruction &I) { return true; }

void FindLazyfiableAnalysis::analyzeCall(CallInst *CI) {
  Function *Callee = CI->getCalledFunction();
  if (Callee == nullptr || Callee->isDeclaration()) {
    return;
  }

  for (auto &arg : CI->args()) {
    if (Instruction *I = dyn_cast<Instruction>(&arg)) {
      unsigned int index = CI->getArgOperandNo(&arg);
      if (isArgumentComplex(*I)) {
        auto pair = std::make_pair(Callee, index);
        _lazyfiableCallSitesStats.insert(pair);
        _lazyfiableCallSites.insert(std::make_pair(CI, index));
      }
    }
  }
}

/// Removes dummy functions in dummyFunctions from the Module.
/// These functions are added by addMissingUses.
static void removeDummyFunctions(std::set<Function *> dummyFunctions) {
  for (auto &F : dummyFunctions) {
    for (auto User : F->users()) {
      CallInst *dummyFunCall = (CallInst *)User;
      dummyFunCall->eraseFromParent();
    }
    F->eraseFromParent();
  }
}

/// Creates a dummy function with signature "void dummy_fun(Type arg)"
/// for a given @param Type, and inserts it into the module, so it
/// can be used to simulate a use of a value of that given type.
static FunctionCallee getDummyFunctionForType(Module &M, Type *Type) {
  LLVMContext &Ctx = M.getContext();
  std::string typeName;
  raw_string_ostream typeNameOs(typeName);
  Type->print(typeNameOs);
  std::string dummyFunctionName = "dummy_fun" + typeNameOs.str();
  FunctionCallee dummyFunction =
      M.getOrInsertFunction(dummyFunctionName, Type::getVoidTy(Ctx), Type);
  return dummyFunction;
}

std::set<Function *> FindLazyfiableAnalysis::addMissingUses(Module &M,
                                                            LLVMContext &Ctx) {
  std::set<Function *> dummyFunctions;
  for (Function &F : M) {
    std::set<Value *> vArgs;
    for (auto &arg : F.args()) {
      if (Value *vArg = dyn_cast<Value>(&arg)) {
        vArgs.insert(vArg);
      }
    }

    for (auto I = inst_begin(F), E = inst_end(F); I != E; ++I) {
      if (PHINode *PN = dyn_cast<PHINode>(&*I)) {
        for (auto &value : PN->operands()) {
          if (!vArgs.count(value)) {
            continue;
          }

          FunctionCallee dummyFunction =
              getDummyFunctionForType(M, value->getType());
          dummyFunctions.insert((Function *)dummyFunction.getCallee());
          Value *args[] = {value};
          BasicBlock *incBlock = PN->getIncomingBlock(value);
          IRBuilder<> builder(incBlock, --incBlock->end());
          builder.CreateCall(dummyFunction, args);
        }
      }
    }
  }
  return dummyFunctions;
}

///
/// To find the most optimization opportunities, we require the IR to have been
/// transformed by the following passes:
///   -mem2reg ->        promotes memory to registers
///   -function-attrs -> infers attributes for functions (such as readonly)
///   -lcssa ->          transforms the program to loop-ssa form
///   -loop-simplify ->  simplifies loops to cannonical forms
///   -mergereturn ->    merges multiple function returns into one
///
/// While it is easy enough to do so from `opt`, when running from `clang` we
/// cannot manipulate when/which passes run, therefore, we run them here manually
/// to guarantee that the IR is in the format we expect.
void runRequiredPasses(Module &M) {
  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;
  PassBuilder PB;

  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  ModulePassManager MPM =
      PB.buildO0DefaultPipeline(llvm::OptimizationLevel::O0);

  MPM.addPass(createModuleToFunctionPassAdaptor(PromotePass()));
  MPM.addPass(createModuleToFunctionPassAdaptor(LCSSAPass()));
  MPM.addPass(createModuleToFunctionPassAdaptor(LoopSimplifyPass()));
  MPM.addPass(
      createModuleToPostOrderCGSCCPassAdaptor(PostOrderFunctionAttrsPass()));
  MPM.addPass(createModuleToFunctionPassAdaptor(UnifyFunctionExitNodesPass()));

  MPM.run(M, MAM);
}

bool FindLazyfiableAnalysis::runOnModule(Module &M) {
  runRequiredPasses(M);

  std::set<Function *> dummyFunctions = addMissingUses(M, M.getContext());

  for (Function &F : M) {
    if (F.isDeclaration() || F.isVarArg()) {
      continue;
    }

    findLazyfiablePaths(F);

    for (auto I = inst_begin(F), E = inst_end(F); I != E; ++I) {
      if (CallInst *CI = dyn_cast<CallInst>(&*I)) {
        analyzeCall(CI);
      }
    }
  }

  removeDummyFunctions(dummyFunctions);

  dump_results();

  return false;
}

void FindLazyfiableAnalysis::dump_results() {
  std::error_code ec;
  raw_fd_ostream outfile("lazyfiable.csv", ec);

  outfile << "function,lazyArg\n";
  for (auto &entry : _promisingFunctionArgs) {
    outfile << entry.first->getName() << "," << entry.second << "\n";
  }

  outfile << "\n\nfunction,arg,callSites\n";
  for (auto &entry : _lazyfiableCallSitesStats) {
    outfile << entry.first->getName() << "," << entry.second << ","
            << entry.second << "\n";
  }

  outfile << "\n\nfunctionsInBoth\n";
  for (auto &entry : _lazyfiableCallSitesStats) {
    if (_promisingFunctionArgs.count(entry) > 0) {
      outfile << entry.first->getName() << "," << entry.second
              << "\n";
    }
  }
}

void FindLazyfiableAnalysis::getAnalysisUsage(AnalysisUsage &AU) const {}

char FindLazyfiableAnalysis::ID = 0;
static RegisterPass<FindLazyfiableAnalysis>
    X("find-lazyfiable", "Wyvern - Find Lazyfiable function arguments.", false,
      false);
