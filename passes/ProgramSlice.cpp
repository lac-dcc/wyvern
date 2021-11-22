#define DEBUG_TYPE "ProgramSlicing"

#include "ProgramSlice.h"

#include <queue>
#include <set>
#include <stack>
#include <tuple>
#include <unordered_map>
#include <utility>

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/Dominators.h"

//#include "../PDG/PDGAnalysis.h"

STATISTIC(InvalidSlices,
          "Slices which contain branches with no post dominator.");

using namespace llvm;

const BasicBlock *getController(const BasicBlock *BB, DominatorTree &DT,
                                PostDominatorTree &PDT) {
  const DomTreeNode *dom_node = DT.getNode(BB);
  while (dom_node) {
    const BasicBlock *dom_BB = dom_node->getBlock();
    if (!PDT.dominates(BB, dom_BB)) {
      return dom_BB;
    } else {
      dom_node = dom_node->getIDom();
    }
  }
  return NULL;
}

const Value *getGate(const BasicBlock *BB) {
  const Value *condition;

  const Instruction *terminator = BB->getTerminator();
  if (const BranchInst *BI = dyn_cast<BranchInst>(terminator)) {
    assert(BI->isConditional() && "Inconditional terminator!");
    condition = BI;
  }

  else if (const SwitchInst *SI = dyn_cast<SwitchInst>(terminator)) {
    condition = SI;
  }

  return condition;
}

const std::unordered_map<const BasicBlock *, SmallVector<const Value *>>
computeGates(Function &F) {
  std::unordered_map<const BasicBlock *, SmallVector<const Value *>> gates;
  DominatorTree DT(F);
  PostDominatorTree PDT;
  PDT.recalculate(F);
  for (const BasicBlock &BB : F) {
    SmallVector<const Value *> BB_gates;
    const unsigned num_preds = pred_size(&BB);
    if (num_preds > 1) {
      LLVM_DEBUG(dbgs() << BB.getName() << ":\n");
      for (const BasicBlock *pred : predecessors(&BB)) {
        LLVM_DEBUG(dbgs() << " - " << pred->getName() << " -> ");
        if (DT.dominates(pred, &BB) && !PDT.dominates(&BB, pred)) {
          LLVM_DEBUG(dbgs() << " DOM " << getGate(pred)->getName() << " -> ");
          BB_gates.push_back(getGate(pred));
        } else {
          const BasicBlock *ctrl_BB = getController(pred, DT, PDT);
          if (ctrl_BB) {
            LLVM_DEBUG(dbgs() << " R-CTRL "
                              << "CTRL_BB: " << ctrl_BB->getName() << " "
                              << getGate(ctrl_BB)->getName());
            BB_gates.push_back(getGate(ctrl_BB));
          }
        }
        LLVM_DEBUG(dbgs() << ";\n");
      }
    }
    gates.emplace(std::make_pair(&BB, BB_gates));
  }
  return gates;
}

std::tuple<std::set<const BasicBlock *>, std::set<const Value *>>
get_data_dependences_for(
    Instruction &I,
    std::unordered_map<const BasicBlock *, SmallVector<const Value *>> &gates) {
  std::set<const Value *> deps;
  std::set<const BasicBlock *> BBs;
  std::set<const Value *> visited;
  std::queue<const Value *> to_visit;

  to_visit.push(&I);
  deps.insert(&I);
  while (!to_visit.empty()) {
    const Value *cur = to_visit.front();
    deps.insert(cur);
    visited.insert(cur);
    to_visit.pop();

    if (const Instruction *dep = dyn_cast<Instruction>(cur)) {
      BBs.insert(dep->getParent());
      for (const Use &U : dep->operands()) {
        if ((!isa<Instruction>(U) && !isa<Argument>(U)) || visited.count(U)) {
          continue;
        }
        to_visit.push(U);
      }
    }

    if (const PHINode *PN = dyn_cast<PHINode>(cur)) {
      for (const Value *gate : gates[PN->getParent()]) {
        if (!visited.count(gate)) {
          to_visit.push(gate);
        }
      }
    }
  }

  return std::make_tuple(BBs, deps);
}

/**
 * Creates a representation of a backwards slice of function @param F in
 * regards to instruction @param I.
 *
 */
ProgramSlice::ProgramSlice(Instruction &Initial, Function &F,
                           CallInst &CallSite) {
  assert(Initial.getParent()->getParent() == &F &&
         "Slicing instruction from different function!");

  //phoenix::ProgramDependenceGraph PDG;
  //PDG.compute_dependences(&F);
  std::unordered_map<const BasicBlock *, SmallVector<const Value *>> gates =
      computeGates(F);
  auto [BBsInSlice, valuesInSlice] = get_data_dependences_for(Initial, gates);
  std::set<const Instruction *> instsInSlice;
  SmallVector<Argument *> depArgs;

  for (auto &val : valuesInSlice) {
    if (Argument *A = dyn_cast<Argument>(const_cast<Value *>(val))) {
      depArgs.push_back(A);
    } else if (const Instruction *I = dyn_cast<Instruction>(val)) {
      instsInSlice.insert(I);
    }
  }

  _instsInSlice = instsInSlice;
  _depArgs = depArgs;
  _initial = &Initial;
  _parentFunction = &F;
  _BBsInSlice = BBsInSlice;
  _CallSite = &CallSite;

  LLVM_DEBUG(printSlice());
}

bool ProgramSlice::verify() {
  DominatorTree DT(*_parentFunction);
  PostDominatorTree PDT;
  PDT.recalculate(*_parentFunction);

  for (auto *I : _instsInSlice) {
    if (const BranchInst *BI = dyn_cast<BranchInst>(I)) {
      bool hasPostDom = false;
      for (const BasicBlock *Succ : BI->successors()) {
        DomTreeNode *SuccNode = PDT.getNode(Succ);
        DomTreeNode *IDom = SuccNode->getIDom();
        hasPostDom |= (_BBsInSlice.count(Succ) > 0);
        while (!hasPostDom && IDom != nullptr) {
          if (_BBsInSlice.count(IDom->getBlock()) > 0) {
            hasPostDom = true;
            break;
          }
          IDom = IDom->getIDom();
        }
      }
      if (!hasPostDom) {
        errs() << "Branch has no postdom in slice: " << *BI
               << " Slice Size: " << _instsInSlice.size()
               << " Function size: " << _parentFunction->size() << "\n";

        ++InvalidSlices;
        return false;
      }
    }
  }
  return true;
}

void ProgramSlice::printSlice() {
  LLVM_DEBUG(dbgs() << "\n\n ==== Slicing function "
                    << _parentFunction->getName() << " with size "
                    << _parentFunction->size() << " in instruction" << *_initial
                    << " ====\n");
  LLVM_DEBUG(dbgs() << "BBs in slice:\n");
  for (const BasicBlock *BB : _BBsInSlice) {
    LLVM_DEBUG(dbgs() << "\t" << BB->getName() << "\n");
    for (const Instruction &I : *BB) {
      if (_instsInSlice.count(&I)) {
        LLVM_DEBUG(dbgs() << "\t\t" << I << "\n";);
      }
    }
  }
  LLVM_DEBUG(dbgs() << "Arguments in slice:\n");
  for (const Argument *A : _depArgs) {
    LLVM_DEBUG(dbgs() << "\t" << *A << "\n";);
  }
  LLVM_DEBUG(dbgs() << "============= \n\n");
}

void ProgramSlice::printFunctions(Function *F) {
  LLVM_DEBUG(dbgs() << "\n======== ORIGINAL FUNCTION ==========\n"
                    << *_parentFunction);
  LLVM_DEBUG(dbgs() << "\n======== SLICED FUNCTION ==========\n" << *F);
}

void ProgramSlice::computeAttractorBlocks() {
  PostDominatorTree PDT;
  PDT.recalculate(*_parentFunction);
  std::map<const BasicBlock *, const BasicBlock *> attractors;

  for (const BasicBlock &BB : *_parentFunction) {
    if (attractors.count(&BB) > 0) {
      continue;
    }

    if (_BBsInSlice.count(&BB) > 0) {
      attractors[&BB] = &BB;
      continue;
    }

    DomTreeNode *OrigBB = PDT.getNode(&BB);
    DomTreeNode *Cand = OrigBB->getIDom();
    while (Cand != nullptr) {
      if (_BBsInSlice.count(Cand->getBlock()) > 0) {
        break;
      }
      Cand = Cand->getIDom();
    }
    if (Cand) {
      attractors[&BB] = Cand->getBlock();
    }
  }
  _attractors = attractors;
}

void ProgramSlice::addDomBranches(DomTreeNode *cur, DomTreeNode *parent,
                                  std::set<DomTreeNode *> &visited) {
  if (_BBsInSlice.count(cur->getBlock())) {
    parent = cur;
  }

  for (DomTreeNode *child : *cur) {
    if (!visited.count(child)) {
      visited.insert(child);
      addDomBranches(child, parent, visited);
    }
    if (_BBsInSlice.count(child->getBlock()) && parent) {
      BasicBlock *parentBB = _origToNewBBmap[parent->getBlock()];
      BasicBlock *childBB = _origToNewBBmap[child->getBlock()];
      if (parentBB->getTerminator() == nullptr) {
        BranchInst *newBranch = BranchInst::Create(childBB, parentBB);
      }
    }
  }
}

void ProgramSlice::rerouteBranches(Function *F) {
  DominatorTree DT(*_parentFunction);
  std::set<DomTreeNode *> visited;
  DomTreeNode *parent = nullptr;

  DomTreeNode *init = DT.getRootNode();
  visited.insert(init);
  if (_BBsInSlice.count(init->getBlock())) {
    parent = init;
  }

  // Visit blocks recursively in order of dominance. If BB1 and BB2 are in
  // slice, BB1 IDom BB2, and BB1 has no terminator, create branch BB1->BB2
  addDomBranches(init, parent, visited);

  // Now iterate over every block in the slice...
  for (BasicBlock &BB : *F) {
    // If block still has no terminator, create an unconditional branch routing
    // it to its attractor.
    if (BB.getTerminator() == nullptr) {
      const BasicBlock *parentBB = _newToOrigBBmap[&BB];
      if (const BranchInst *origBranch =
              dyn_cast<BranchInst>(parentBB->getTerminator())) {
        for (const BasicBlock *suc : origBranch->successors()) {
          BasicBlock *newTarget = _origToNewBBmap[_attractors[suc]];
          if (newTarget != nullptr) {
            BranchInst::Create(newTarget, &BB);
            break;
          }
        }
      }
    } else {
      // Otherwise, the block's original branch was part of the slice...
      Instruction *term = BB.getTerminator();
      if (BranchInst *BI = dyn_cast<BranchInst>(term)) {
        for (unsigned int idx = 0; idx < BI->getNumSuccessors(); ++idx) {
          const BasicBlock *suc = BI->getSuccessor(idx);
          if (suc->getParent() == F) {
            continue;
          }
          const BasicBlock *attractor = _attractors[suc];
          BasicBlock *newSucc = _origToNewBBmap[attractor];
          BI->setSuccessor(idx, newSucc);
          for (Instruction &I : *newSucc) {
            if (!isa<PHINode>(&I)) {
              continue;
            }
            PHINode *PN = dyn_cast<PHINode>(&I);
            PN->replaceIncomingBlockWith(suc, &BB);
          }
        }
      } else if (SwitchInst *SI = dyn_cast<SwitchInst>(term)) {
        for (unsigned int idx = 0; idx < SI->getNumSuccessors(); ++idx) {
          const BasicBlock *suc = SI->getSuccessor(idx);
          if (suc->getParent() == F) {
            continue;
          }
          const BasicBlock *attractor = _attractors[suc];
          BasicBlock *newSucc = _origToNewBBmap[attractor];
          SI->setSuccessor(idx, newSucc);
          for (Instruction &I : *newSucc) {
            if (!isa<PHINode>(&I)) {
              continue;
            }
            PHINode *PN = dyn_cast<PHINode>(&I);
            PN->replaceIncomingBlockWith(suc, &BB);
          }
        }
      }
    }
  }
}

bool ProgramSlice::canOutline() {
  DominatorTree DT(*_parentFunction);
  LoopInfo LI = LoopInfo(DT);
  for (const Instruction *I : _instsInSlice) {
    if (I->mayHaveSideEffects()) {
      LLVM_DEBUG(dbgs() << "Cannot outline because inst may have side effects: "
                        << *I << "\n");
      return false;
    }
  }

  if (LI.getLoopDepth(_CallSite->getParent()) > 0) {
    for (const BasicBlock *BB : _BBsInSlice) {
      if (LI.getLoopDepth(BB) <= LI.getLoopDepth(_CallSite->getParent())) {
        errs() << "BB " << BB->getName()
               << " is in same or lower loop depth as CallSite BB "
               << _CallSite->getParent()->getName() << "\n";
        return false;
      }
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
SmallVector<const Value *> ProgramSlice::getOrigFunctionArgs() {
  SmallVector<const Value *> args;
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
void ProgramSlice::insertNewBB(const BasicBlock *originalBB, Function *F) {
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
  for (const BasicBlock *BB : _BBsInSlice) {
    insertNewBB(BB, F);
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

/**
 * Adds terminating branches to BasicBlocks in function @param F,
 * for BBs whose branches were not included in the slice but
 * which are necessary to replicate the control flow of the
 * original function.
 */
void ProgramSlice::addMissingTerminators(Function *F) {
  for (BasicBlock &BB : *F) {
    if (BB.getTerminator() == nullptr) {
      const Instruction *originalTerminator =
          _newToOrigBBmap[&BB]->getTerminator();
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
    errs() << "In block: " << BB.getName() << "\n";
    if (BB.hasNPredecessors(0)) {
      errs() << "Has 0!\n";
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

  if (exit->getTerminator()) {
    exit->getTerminator()->eraseFromParent();
  }

  return ReturnInst::Create(F->getParent()->getContext(), _Imap[_initial],
                            exit);
}

/**
 * Returns the types of the original function's formal parameters
 * _which are included in the slice_, so the sliced function's
 * signature can be created to match it.
 *
 */
SmallVector<Type *> ProgramSlice::getInputArgTypes() {
  SmallVector<Type *> argTypes;
  for (const Argument *A : _depArgs) {
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

  computeAttractorBlocks();
  populateFunctionWithBBs(F);
  populateBBsWithInsts(F);
  reorganizeUses(F);
  rerouteBranches(F);
  addReturnValue(F);
  reorderBlocks(F);
  verifyFunction(*F);
  printFunctions(F);

  return F;
}

void ProgramSlice::addMemoizationCode(Function *F, ReturnInst *new_ret) {
  assert(isa<PointerType>(F->arg_begin()->getType()) &&
         "Memoized function does not have PointerType argument!\n");

  // boilerplate constants needed to index structs/pointers
  LLVMContext &Ctx = F->getParent()->getContext();
  ConstantInt *i32_zero = ConstantInt::get(Type::getInt32Ty(Ctx), 0);
  ConstantInt *i32_one = ConstantInt::get(Type::getInt32Ty(Ctx), 1);
  ConstantInt *i32_two = ConstantInt::get(Type::getInt32Ty(Ctx), 2);
  ConstantInt *i8_one = ConstantInt::get(Type::getInt8Ty(Ctx), 1);

  // create new entry block and block to insert memoed return
  BasicBlock *oldEntry = &F->getEntryBlock();
  BasicBlock *newEntry =
      BasicBlock::Create(Ctx, "_wyvern_memo_entry", F, &F->getEntryBlock());
  BasicBlock *memoRetBlock =
      BasicBlock::Create(Ctx, "_wyvern_memo_ret", F, oldEntry);

  // load addresses and values for thunk struct members
  Value *argValue = F->arg_begin();
  GetElementPtrInst *memoedValueGep = GetElementPtrInst::CreateInBounds(
      argValue, {i32_zero, i32_one}, "_memo_val_addr", newEntry);
  LoadInst *memoedValueLoad =
      new LoadInst(memoedValueGep->getResultElementType(), memoedValueGep,
                   "_memo_val", newEntry);
  GetElementPtrInst *memoFlagGep = GetElementPtrInst::CreateInBounds(
      argValue, {i32_zero, i32_two}, "_memo_flag_addr", newEntry);
  LoadInst *memoFlagLoad = new LoadInst(memoFlagGep->getResultElementType(),
                                        memoFlagGep, "_memo_flag", newEntry);

  // add if (memoFlag == true) { return memo_val; }
  CastInst *toBool = CastInst::CreateTruncOrBitCast(
      memoFlagLoad, IntegerType::getInt1Ty(Ctx), "_memo_flag_bool", newEntry);
  BranchInst *memoCheckBranch =
      BranchInst::Create(memoRetBlock, oldEntry, toBool, newEntry);
  ReturnInst *memoedValueRet =
      ReturnInst::Create(Ctx, memoedValueLoad, memoRetBlock);

  // store computed value and update memoization flag
  StoreInst *memoFlagStore = new StoreInst(i8_one, memoFlagGep, new_ret);
  StoreInst *memoedValueStore =
      new StoreInst(new_ret->getReturnValue(), memoedValueGep, new_ret);
}

/**
 * Outlines the given slice into a standalone Function, which
 * encapsulates the computation of the original value in
 * regards to which the slice was created. Adds memoization
 * code so that the function saves its evaluated value and
 * returns it on successive executions.
 */
Function *ProgramSlice::memoizedOutline() {
  Module *M = _initial->getParent()->getParent()->getParent();
  LLVMContext &Ctx = M->getContext();

  StructType *thunkStructType = StructType::create(Ctx);
  PointerType *thunkStructPtrType = PointerType::get(thunkStructType, 0);
  FunctionType *thunkFunctionType =
      FunctionType::get(_initial->getType(), {thunkStructPtrType}, false);
  SmallVector<Type *> thunkTypes = {thunkFunctionType->getPointerTo(),
                                    thunkFunctionType->getReturnType(),
                                    IntegerType::get(Ctx, 8)};

  thunkStructType->setBody(thunkTypes);

  std::string functionName =
      "_wyvern_slice_" + _initial->getParent()->getParent()->getName().str() +
      "_" + _initial->getName().str();
  Function *F = Function::Create(thunkFunctionType, Function::ExternalLinkage,
                                 functionName, M);

  F->arg_begin()->setName("_wyvern_thunk");

  populateFunctionWithBBs(F);
  populateBBsWithInsts(F);
  reorganizeUses(F);
  ReturnInst *new_ret = addReturnValue(F);
  reorderBlocks(F);
  addMemoizationCode(F, new_ret);

  verifyFunction(*F);
  verifyFunction(*_initial->getParent()->getParent());

  LLVM_DEBUG(dbgs() << "\n======== ORIGINAL FUNCTION ==========\n"
                    << *_initial->getParent()->getParent());
  LLVM_DEBUG(dbgs() << "\n======== SLICED FUNCTION ==========\n" << *F);

  return F;
}