#define DEBUG_TYPE "ProgamSlicing"

#include "ProgramSlice.h"

#include "llvm/Support/Debug.h"
#define DEBUG_TYPE "ProgamSlicing"

using namespace llvm;

/**
 * Creates a representation of a backwards slice of function @param F in
 * regards to instruction @param I.
 *
 */
ProgramSlice::ProgramSlice(Instruction &I, Function &F) {
  assert(I.getParent()->getParent() == &F &&
         "Slicing instruction from different function!");

  phoenix::ProgramDependenceGraph PDG;
  PDG.compute_dependences(&F);
  std::set<Value *> valuesInSlice = PDG.get_dependences_for(&I);
  std::set<Instruction *> instsInSlice;
  SmallVector<Argument *> depArgs;

  LLVM_DEBUG(dbgs() << "Slicing function " << F.getName() << " in instruction "
                    << I << "\n");
  LLVM_DEBUG(dbgs() << "Values in slice:\n");
  for (auto &val : valuesInSlice) {
    if (Argument *A = dyn_cast<Argument>(val)) {
      LLVM_DEBUG(dbgs() << "Arg: " << *A << "\n";);
      depArgs.push_back(A);
    } else if (Instruction *I = dyn_cast<Instruction>(val)) {
      LLVM_DEBUG(dbgs() << "Inst: " << *I << "\n";);
      instsInSlice.insert(I);
    }
  }

  _instsInSlice = instsInSlice;
  _depArgs = depArgs;
  _initial = &I;
  _parentFunction = &F;
}

bool ProgramSlice::canOutline() {
  for (Instruction *I : _instsInSlice) {
    if (I->mayHaveSideEffects()) {
      LLVM_DEBUG(dbgs() << "Cannot outline because inst may have side effects: "
                        << *I << "\n");
      return false;
    }
  }
  // we haven't implemented lazyfication for input argument-dependent slices yet
  return (_depArgs.size() == 0);
}

/**
 * Returns the arguments from the original function which
 * are part of the slice. Is used externally to match formal
 * parameters with actual parameters when generating calls to
 * outlined slice functions.
 *
 */
SmallVector<Value *> ProgramSlice::getOrigFunctionArgs() {
  SmallVector<Value *> args;
  for (auto &arg : _depArgs) {
    args.push_back(cast<Value>(arg));
  }
  return args;
}

/**
 * Inserts a new BasicBlock in Function @param F, corresponding
 * to the @param originalBB from the original function being
 * sliced.
 *
 */
void ProgramSlice::insertNewBB(BasicBlock *originalBB, Function *F) {
  auto originalName = originalBB->getName();
  std::string newBBName = "sliceclone_" + originalName.str();
  BasicBlock *newBB =
      BasicBlock::Create(F->getParent()->getContext(), newBBName, F);
  _origToNewBBmap.insert(std::make_pair(originalBB, newBB));
  _newToOrigBBmap.insert(std::make_pair(newBB, originalBB));
}

/**
 * Populates function @param F with BasicBlocks, corresponding
 * to the BBs in the original function being sliced which
 * contained instructions included in the slice.
 *
 */
void ProgramSlice::populateFunctionWithBBs(Function *F) {
  for (Instruction *I : _instsInSlice) {
    if (_origToNewBBmap.count(I->getParent()) == 0) {
      insertNewBB(I->getParent(), F);
    }

    for (Use &U : I->operands()) {
      if (BasicBlock *origBB = dyn_cast<BasicBlock>(&U)) {
        if (_origToNewBBmap.count(origBB) == 0) {
          insertNewBB(origBB, F);
        }
      }
    }

    if (PHINode *PN = dyn_cast<PHINode>(I)) {
      for (BasicBlock *BB : PN->blocks()) {
        if (_origToNewBBmap.count(BB) == 0) {
          insertNewBB(BB, F);
        }
      }
    }
  }
}

/**
 * Adds slice instructions to function @param F, corresponding
 * to instructions in the original function.
 *
 */
void ProgramSlice::populateBBsWithInsts(Function *F) {
  for (BasicBlock &BB : *_parentFunction) {
    for (Instruction &origInst : BB) {
      if (_instsInSlice.count(&origInst)) {
        Instruction *newInst = origInst.clone();
        _Imap.insert(std::make_pair(&origInst, newInst));
        IRBuilder<> builder(_origToNewBBmap[&BB]);
        builder.Insert(newInst);
      }
    }
  }
}

/**
 * Fixes the instruction/argument/BB uses in new function @param F,
 * to use their corresponding versions in the sliced function, rather
 * than the originals from whom they were cloned.
 *
 */
void ProgramSlice::reorganizeUses(Function *F) {
  for (auto &pair : _origToNewBBmap) {
    BasicBlock *originalBB = pair.first;
    originalBB->replaceUsesWithIf(pair.second, [F](Use &U) {
      auto *UserI = dyn_cast<Instruction>(U.getUser());
      return UserI && UserI->getParent()->getParent() == F;
    });

    for (auto &pair : _Imap) {
      Instruction *originalInst = pair.first;
      Instruction *newInst = pair.second;

      if (PHINode *PN = dyn_cast<PHINode>(newInst)) {
        for (BasicBlock *BB : PN->blocks()) {
          if (_origToNewBBmap.count(BB)) {
            PN->replaceIncomingBlockWith(BB, _origToNewBBmap[BB]);
          }
        }
      }

      originalInst->replaceUsesWithIf(newInst, [F](Use &U) {
        auto *UserI = dyn_cast<Instruction>(U.getUser());
        return UserI && UserI->getParent()->getParent() == F;
      });
    }

    for (auto &pair : _argMap) {
      Argument *origArg = pair.first;
      Argument *newArg = pair.second;

      origArg->replaceUsesWithIf(newArg, [F](Use &U) {
        auto *UserI = dyn_cast<Instruction>(U.getUser());
        return UserI && UserI->getParent()->getParent() == F;
      });
    }
  }
}

/**
 * Adds terminating branches to BasicBlocks in function @param F,
 * for BBs whose branches were not included in the slice but
 * which are necessary to replicate the control flow of the
 * original function.
 */
void ProgramSlice::addMissingTerminators(Function *F) {
  for (BasicBlock &BB : *F) {
    if (BB.getTerminator() == nullptr) {
      Instruction *originalTerminator = _newToOrigBBmap[&BB]->getTerminator();
      Instruction *newTerminator = originalTerminator->clone();
      IRBuilder<> builder(&BB);
      builder.Insert(newTerminator);
    }
  }
}

/**
 * Reorders basic blocks in the new function @param F, to make
 * sure that the sliced function's entry block (the only one
 * with no predecessors) is first in the layout.
 */
void ProgramSlice::reorderBlocks(Function *F) {
  BasicBlock *realEntry = nullptr;
  for (BasicBlock &BB : *F) {
    if (BB.hasNPredecessors(0)) {
      realEntry = &BB;
    }
  }
  realEntry->moveBefore(&F->getEntryBlock());
}

/**
 * Adds a return instruction to function @param F, which returns
 * the value that is computed by the sliced function.
 *
 */
ReturnInst *ProgramSlice::addReturnValue(Function *F) {
  BasicBlock *exit = _Imap[_initial]->getParent();

  exit->getTerminator()->eraseFromParent();
  return
      ReturnInst::Create(F->getParent()->getContext(), _Imap[_initial], exit);
}

/**
 * Returns the types of the original function's formal parameters
 * _which are included in the slice_, so the sliced function's
 * signature can be created to match it.
 *
 */
SmallVector<Type *> ProgramSlice::getInputArgTypes() {
  SmallVector<Type *> argTypes;
  for (Argument *A : _depArgs) {
    argTypes.emplace_back(A->getType());
  }
  return argTypes;
}

/**
 * Outlines the given slice into a standalone Function, which
 * encapsulates the computation of the original value in
 * regards to which the slice was created.
 */
Function *ProgramSlice::outline() {
  Module *M = _initial->getParent()->getParent()->getParent();
  LLVMContext &Ctx = M->getContext();

  SmallVector<Type *> inputTypes = getInputArgTypes();
  FunctionType *FT = FunctionType::get(_initial->getType(), inputTypes, false);
  std::string functionName =
      "_wyvern_slice_" + _initial->getParent()->getParent()->getName().str() +
      "_" + _initial->getName().str();
  Function *F =
      Function::Create(FT, Function::ExternalLinkage, functionName, M);

  unsigned id = 0;
  for (Argument &arg : F->args()) {
    arg.setName(_depArgs[id]->getName());
    _argMap.insert(std::make_pair(_depArgs[id++], &arg));
  }

  populateFunctionWithBBs(F);
  populateBBsWithInsts(F);
  addMissingTerminators(F);
  reorganizeUses(F);
  addReturnValue(F);
  reorderBlocks(F);

  verifyFunction(*F);

  LLVM_DEBUG(dbgs() << "\n======== ORIGINAL FUNCTION ==========\n"
                    << *_initial->getParent()->getParent());
  LLVM_DEBUG(dbgs() << "\n======== SLICED FUNCTION ==========\n" << *F);

  return F;
}