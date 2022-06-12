#include "ProgramSlice.h"

#include <map>
#include <queue>
#include <set>
#include <stack>
#include <tuple>
#include <unordered_map>
#include <utility>

#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasSetTracker.h"
#include "llvm/Analysis/CaptureTracking.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include <random>

#define DEBUG_TYPE "ProgramSlicing"

STATISTIC(InvalidSlices,
          "Slices which contain branches with no post dominator.");

using namespace llvm;

/// Returns the block whose predicate should control the phi-functions in BB
static const BasicBlock *getController(const BasicBlock *BB, DominatorTree &DT,
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

/// Returns the predicate of the given basic block, which will be used to gate
/// another basic block's phi-functions.
static const Value *getGate(const BasicBlock *BB) {
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

/// Computes the gates for all basic blocks in the slice. The data structure
/// holding the gates data is a map of each basic block to a vector of its
/// gates.
static const std::unordered_map<const BasicBlock *, SmallVector<const Value *>>
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

/// Computes the backwards data dependences for the given instruction, to
/// compute which instructions should be part of the slice. Using the
/// phi-function gate information contained in gates, control dependencies can
/// also be tracked as data dependences. Thus, this function is enough to
/// compute all dependencies necessary to building a slice.
static std::tuple<std::set<const BasicBlock *>, std::set<const Value *>>
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
    to_visit.pop();

    if (const Instruction *dep = dyn_cast<Instruction>(cur)) {
      BBs.insert(dep->getParent());
      for (const Use &U : dep->operands()) {
        if ((!isa<Instruction>(U) && !isa<Argument>(U)) || visited.count(U)) {
          continue;
        }
        visited.insert(U);
        to_visit.push(U);
      }
    }

    if (const PHINode *PN = dyn_cast<PHINode>(cur)) {
      for (const BasicBlock *BB : PN->blocks()) {
        BBs.insert(BB);
      }
      for (const Value *gate : gates[PN->getParent()]) {
        if (gate && !visited.count(gate)) {
          to_visit.push(gate);
        }
      }
    }
  }

  return std::make_tuple(BBs, deps);
}

ProgramSlice::ProgramSlice(Instruction &Initial, Function &F,
                           CallInst &CallSite, AAResults *AA,
                           TargetLibraryInfo &TLI)
    : _AA(AA), _TLI(TLI), _initial(&Initial), _parentFunction(&F) {
  assert(Initial.getParent()->getParent() == &F &&
         "Slicing instruction from different function!");

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
  _BBsInSlice = BBsInSlice;
  _CallSite = &CallSite;

  // We need to pre-compute struct types, because if we build it everytime
  // it's needed, LLVM creates multiple types with the same structure but
  // different names.
  _thunkStructType = computeStructType(false /*memo*/);
  _memoizedThunkStructType = computeStructType(true /*memo*/);

  computeAttractorBlocks();

  LLVM_DEBUG(printSlice());
}

/// Computes the layout of the struct type that should be used to lazify
/// instances of this delegate function.
StructType *ProgramSlice::computeStructType(bool memo) {
  Module *M = _initial->getParent()->getParent()->getParent();
  LLVMContext &Ctx = M->getContext();

  StructType *thunkStructType = StructType::create(Ctx);
  PointerType *thunkStructPtrType = PointerType::get(thunkStructType, 0);
  FunctionType *delegateFunctionType =
      FunctionType::get(_initial->getType(), {thunkStructPtrType}, false);
  SmallVector<Type *> thunkTypes;
  if (memo) {
    // Memoized thunks have the form:
    //   T (fptr)(struct thunk *thk);
    //   T memo_val;
    //   bool memo_flag;
    //   ... (environment)
    thunkTypes = {delegateFunctionType->getPointerTo(),
                  delegateFunctionType->getReturnType(),
                  IntegerType::get(Ctx, 1)};
  } else {
    // Non-memoized thunks have the form:
    //  T (fptr)(struct thunk *thk);
    //  ... (environment)
    thunkTypes = {delegateFunctionType->getPointerTo()};
  }

  for (auto &arg : _depArgs) {
    thunkTypes.push_back(arg->getType());
  }

  thunkStructType->setBody(thunkTypes);
  thunkStructType->setName("_wyvern_thunk_type");

  return thunkStructType;
}

StructType *ProgramSlice::getThunkStructType(bool memo) {
  if (memo) {
    return _memoizedThunkStructType;
  }
  return _thunkStructType;
}

/// Prints the slice. Used for debugging.
void ProgramSlice::printSlice() {
  LLVM_DEBUG(dbgs() << "\n\n ==== Slicing function "
                    << _parentFunction->getName() << " with size "
                    << _parentFunction->size() << " in instruction" << *_initial
                    << " ====\n");
  LLVM_DEBUG(dbgs() << "==== Call site: " << *_CallSite << " ====\n");
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

/// Print original and sliced function. Used for debugging.
void ProgramSlice::printFunctions(Function *F) {
  LLVM_DEBUG(dbgs() << "\n======== ORIGINAL FUNCTION ==========\n"
                    << *_parentFunction);
  LLVM_DEBUG(dbgs() << "\n======== SLICED FUNCTION ==========\n" << *F);
}

/// Computes the attractor blocks (first dominator) for each basic block in the
/// original function. The map of basic blocks to their attractors is used to
/// reroute control flow in the outlined delegate function.
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

/// Adds branches from immediate dominators which existed in the original
/// function to the slice.
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

/// When cloning PHINodes from the original function, some PHIs may have
/// leftover incoming blocks which were not included in the slice. Therefore,
/// these blocks are now invalid, as they are not predecessors of the new PHI.
/// This function removes these.
void updatePHINodes(Function *F) {
  for (BasicBlock &BB : *F) {
    std::set<BasicBlock *> preds(pred_begin(&BB), pred_end(&BB));
    for (auto I_it = BB.begin(); I_it != BB.end();) {
      PHINode *PN = dyn_cast<PHINode>(I_it);
      if (!PN) {
        break;
      }
      ++I_it;
      for (unsigned PI = 0, PE = PN->getNumIncomingValues(); PI != PE; ++PI) {
        BasicBlock *incBB = PN->getIncomingBlock(PI);
        if (incBB && !preds.count(incBB)) {
          PN->removeIncomingValue(incBB);
        }
      }
    }
  }
}

/// Reroutes branches in the slice, to properly build the control flow of the
/// delegate function, once instructions and basic blocks from the original
/// function have been possibly removed.
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

  // Save list of PHI nodes to update. Old blocks should be replaced by
  // new blocks as predecessors in merging values. We store PHIs to update
  // at the end of the function to avoid invalidating iterators if we
  // modify in-place.
  std::map<PHINode *, std::pair<BasicBlock *, BasicBlock *>> PHIsToUpdate;

  // Add an unreachable block to be the target of branches that should
  // be removed.
  BasicBlock *unreachableBlock =
      BasicBlock::Create(F->getContext(), "_wyvern_unreachable", F);
  UnreachableInst *unreach =
      new UnreachableInst(F->getContext(), unreachableBlock);

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
          if (!newTarget) {
            continue;
          }
          BranchInst::Create(newTarget, &BB);
          // If new successor has any PHINodes that merged a path from a block
          // that was dominated by this block, update its incoming block to
          // be this instead.
          for (Instruction &I : *newTarget) {
            if (!isa<PHINode>(I)) {
              continue;
            }
            PHINode *phi = cast<PHINode>(&I);
            for (BasicBlock *newTargetPHIBB : phi->blocks()) {
              if (newTargetPHIBB->getParent() != F) {
                DomTreeNode *OrigBB = DT.getNode(newTargetPHIBB);
                DomTreeNode *Cand = OrigBB->getIDom();
                while (Cand != nullptr) {
                  if (Cand->getBlock() == parentBB) {
                    break;
                  }
                  Cand = Cand->getIDom();
                }
                if (Cand) {
                  phi->replaceIncomingBlockWith(newTargetPHIBB, &BB);
                }
              }
            }
          }
          break;
        }
      }
    } else {
      // Otherwise, the block's original branch was part of the slice...
      Instruction *term = BB.getTerminator();
      if (BranchInst *BI = dyn_cast<BranchInst>(term)) {
        for (unsigned int idx = 0; idx < BI->getNumSuccessors(); ++idx) {
          BasicBlock *suc = BI->getSuccessor(idx);
          if (suc->getParent() == F) {
            continue;
          }
          const BasicBlock *attractor = _attractors[suc];
          BasicBlock *newSucc = _origToNewBBmap[attractor];

          if (!newSucc) {
            suc->replaceUsesWithIf(unreachableBlock, [F](Use &U) {
              auto *UserI = dyn_cast<Instruction>(U.getUser());
              return UserI && UserI->getParent()->getParent() == F;
            });
            BI->setSuccessor(idx, unreachableBlock);
            continue;
          }

          BI->setSuccessor(idx, newSucc);
          for (Instruction &I : *newSucc) {
            if (!isa<PHINode>(I)) {
              continue;
            }
            PHINode *phi = cast<PHINode>(&I);
            phi->replaceIncomingBlockWith(suc, &BB);
          }
        }
      } else if (SwitchInst *SI = dyn_cast<SwitchInst>(term)) {
        for (unsigned int idx = 0; idx < SI->getNumSuccessors(); ++idx) {
          BasicBlock *suc = SI->getSuccessor(idx);
          if (suc->getParent() == F) {
            continue;
          }
          const BasicBlock *attractor = _attractors[suc];
          BasicBlock *newSucc = _origToNewBBmap[attractor];

          if (!newSucc) {
            suc->replaceUsesWithIf(unreachableBlock, [F](Use &U) {
              auto *UserI = dyn_cast<Instruction>(U.getUser());
              return UserI && UserI->getParent()->getParent() == F;
            });
            SI->setSuccessor(idx, unreachableBlock);
            continue;
          }

          SI->setSuccessor(idx, newSucc);
          for (Instruction &I : *newSucc) {
            if (!isa<PHINode>(I)) {
              continue;
            }
            PHINode *phi = cast<PHINode>(&I);
            phi->replaceIncomingBlockWith(suc, &BB);
          }
        }
      }
    }
  }

  // If unreachable block was never used, remove it so we avoid mistaking it
  // as a potential entry block (due to it having no predecessors)
  if (unreachableBlock->hasNPredecessors(0)) {
    unreachableBlock->eraseFromParent();
  }

  updatePHINodes(F);
}

bool ProgramSlice::canOutline() {
  DominatorTree DT(*_parentFunction);
  LoopInfo LI = LoopInfo(DT);
  AliasSetTracker AST(*_AA);

  // Build alias sets for stores and function calls in the function.
  // Note that we only care about memory locations that are part of the
  // slice, or which are loaded by loads in the slice.
  for (BasicBlock &BB : *_parentFunction) {
    for (Instruction &I : BB) {
      if (StoreInst *SI = dyn_cast<StoreInst>(&I)) {
        AST.add(SI);
      } else if (CallBase *CB = dyn_cast<CallBase>(&I)) {
        if (CB == _CallSite) {
          continue;
        }
        AST.add(CB);
      }
    }
  }

  for (const Instruction *I : _instsInSlice) {
    if (I->mayThrow()) {
      errs() << "Cannot outline slice because inst may throw: " << *I << "\n";
      return false;
    }

    if (const CallBase *CB = dyn_cast<CallBase>(I)) {
      if (!CB->getCalledFunction()) {
        errs() << "Cannot outline slice because instruction calls unknown "
                  "function: "
               << *CB << "\n";
        return false;
      }

      LibFunc builtin;
      if (CB->getCalledFunction()->isDeclaration() &&
          !_TLI.getLibFunc(*CB, builtin)) {
        errs() << "Cannot outline slice because instruction calls non-builtin "
                  "function with no body: "
               << *CB << "\n";
        return false;
      }
    }

    // For instructions that may read or write to memory, we need some special
    // care to avoid load/store reordering and/or side effects.
    if (I->mayReadOrWriteMemory()) {
      if (const LoadInst *LI = dyn_cast<LoadInst>(I)) {
        // For loads, we invalidate outlining if its address can be modified.
        // This is possible if the memory location pointed to by the load is
        // written/modified by any possibly aliasing pointer or clobbering
        // function call.
        if (AST.getAliasSetFor(MemoryLocation::get(LI)).isMod()) {
          errs()
              << "Cannot outline slice because load address can be modified: "
              << *LI << "\n";
          return false;
        }

      } else if (const CallBase *CB = dyn_cast<CallBase>(I)) {
        // For function calls, if the call has any side effects (as in, is not
        // read-only), we can't outline the slice.
        if (!_AA->onlyReadsMemory(CB->getCalledFunction())) {
          errs() << "Cannot outline because call may write to memory: " << *CB
                 << "\n";
          return false;
        }
      } else {
        errs() << "Cannot outline because inst may read or write to memory: "
               << *I << "\n";
        return false;
      }
    }

    else if (!I->willReturn()) {
      errs() << "Cannot outline because inst may not return: " << *I << "\n";
      return false;
    }

    for (const Value *arg : _CallSite->args()) {
      if (arg == _initial) {
        continue;
      }
      if (arg->getType()->isPointerTy() && I->getType()->isPointerTy()) {
        if (_AA->alias(arg, I) != AliasResult::NoAlias) {
          errs() << "Cannot outline slice because pointer used in slice is "
                    "passed as argument to callee.\nPointer: "
                 << *I << "\nArgument: " << *arg << "\n";
          return false;
        }
      }
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

  if (isa<AllocaInst>(_initial)) {
    LLVM_DEBUG(
        (dbgs()
         << "Cannot outline slice due to slicing criteria being an alloca!\n"));
    return false;
  }

  // LCSSA may insert PHINodes with only a single incoming block. In some cases,
  // these PHINodes can be added into the slice, but the conditional for the
  // loop that generated them is not. When eliminating the PHINode, we'd
  // generate invalid code, so we avoid optimizing these cases temporarily.
  if (PHINode *PN = dyn_cast<PHINode>(_initial)) {
    if (PN->getNumIncomingValues() == 1) {
      BasicBlock *incBB = PN->getIncomingBlock(0);
      if (_instsInSlice.count(incBB->getTerminator()) == 0) {
        return false;
      }
    }
  }

  return true;
}

SmallVector<Value *> ProgramSlice::getOrigFunctionArgs() {
  SmallVector<Value *> args;
  for (auto &arg : _depArgs) {
    args.push_back(cast<Value>(arg));
  }
  return args;
}

/// Inserts a new BasicBlock in Function @param F, corresponding
/// to the @param originalBB from the original function being
/// sliced.
void ProgramSlice::insertNewBB(const BasicBlock *originalBB, Function *F) {
  auto originalName = originalBB->getName();
  std::string newBBName = "sliceclone_" + originalName.str();
  BasicBlock *newBB =
      BasicBlock::Create(F->getParent()->getContext(), newBBName, F);
  _origToNewBBmap.insert(std::make_pair(originalBB, newBB));
  _newToOrigBBmap.insert(std::make_pair(newBB, originalBB));
}

/// Populates function @param F with BasicBlocks, corresponding
/// to the BBs in the original function being sliced which
/// contained instructions included in the slice.
void ProgramSlice::populateFunctionWithBBs(Function *F) {
  for (const BasicBlock *BB : _BBsInSlice) {
    insertNewBB(BB, F);
  }
}

/// Adds slice instructions to function @param F, corresponding
/// to instructions in the original function.
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

/// Fixes the instruction/argument/BB uses in new function @param F,
/// to use their corresponding versions in the sliced function, rather
/// than the originals from whom they were cloned.
void ProgramSlice::reorganizeUses(Function *F) {
  IRBuilder<> builder(F->getContext());

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
}

/// Adds terminating branches to BasicBlocks in function @param F,
/// for BBs whose branches were not included in the slice but
/// which are necessary to replicate the control flow of the
/// original function.
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

/// Reorders basic blocks in the new function @param F, to make
/// sure that the sliced function's entry block (the only one
/// with no predecessors) is first in the layout. This is necessary
/// because LLVM assumes the first block of a function is always its
/// entry block.
void ProgramSlice::reorderBlocks(Function *F) {
  BasicBlock *realEntry = nullptr;
  for (BasicBlock &BB : *F) {
    if (BB.hasNPredecessors(0)) {
      realEntry = &BB;
    }
  }
  realEntry->moveBefore(&F->getEntryBlock());
}

/// Adds a return instruction to function @param F, which returns
/// the value that is computed by the sliced function.
ReturnInst *ProgramSlice::addReturnValue(Function *F) {
  BasicBlock *exit = _Imap[_initial]->getParent();

  if (exit->getTerminator()) {
    exit->getTerminator()->eraseFromParent();
  }

  return ReturnInst::Create(F->getParent()->getContext(), _Imap[_initial],
                            exit);
}

/// Updates the delegate function's code to make use of parameters provided by
/// the environment in its thunk, rather than the original function's values.
void ProgramSlice::insertLoadForThunkParams(Function *F, bool memo) {
  IRBuilder<> builder(F->getContext());

  BasicBlock &entry = F->getEntryBlock();
  Argument *thunkStructPtr = F->arg_begin();
  StructType *thunkStructType = getThunkStructType(memo);

  assert(isa<PointerType>(thunkStructPtr->getType()) &&
         "Sliced function's first argument does not have struct pointer type!");

  builder.SetInsertPoint(&*(entry.getFirstInsertionPt()));

  // memo thunk arguments start at 3, due to the memo flag and memoed value
  // taking up two slots
  unsigned int i = memo ? 3 : 1;
  for (auto &arg : _depArgs) {
    Value *new_arg_addr =
        builder.CreateStructGEP(thunkStructType, thunkStructPtr, i,
                                "_wyvern_arg_addr_" + arg->getName());
    Value *new_arg =
        builder.CreateLoad(thunkStructType->getStructElementType(i),
                           new_arg_addr, "_wyvern_arg_" + arg->getName());
    arg->replaceUsesWithIf(new_arg, [F](Use &U) {
      auto *UserI = dyn_cast<Instruction>(U.getUser());
      return UserI && UserI->getParent()->getParent() == F;
    });

    _argMap[arg] = new_arg;
    ++i;
  }
}

/// Outlines the given slice into a standalone Function, which
/// encapsulates the computation of the original value in
/// regards to which the slice was created.
Function *ProgramSlice::outline() {
  StructType *thunkStructType = getThunkStructType(false);
  PointerType *thunkStructPtrType = thunkStructType->getPointerTo();
  FunctionType *delegateFunctionType =
      FunctionType::get(_initial->getType(), {thunkStructPtrType}, false);

  // generate a random number to use as suffix for delegate function, to avoid
  // naming conflicts
  // NOTE: we cannot use a simple counter that gets incremented
  // on every slice here, because when optimizing per translation unit, the same
  // function may be sliced across different translation units
  std::random_device rd;
  std::mt19937 mt(rd());
  std::uniform_int_distribution<int64_t> dist(1, 1000000000);
  uint64_t random_num = dist(mt);
  std::string functionName =
      "_wyvern_slice_" + _initial->getParent()->getParent()->getName().str() +
      "_" + _initial->getName().str() + std::to_string(random_num);
  Function *F = Function::Create(
      delegateFunctionType, Function::ExternalLinkage, functionName,
      _initial->getParent()->getParent()->getParent());

  F->arg_begin()->setName("_wyvern_thunkptr");

  populateFunctionWithBBs(F);
  populateBBsWithInsts(F);
  reorganizeUses(F);
  rerouteBranches(F);
  addReturnValue(F);
  reorderBlocks(F);
  insertLoadForThunkParams(F, false /*memo*/);
  verifyFunction(*F);
  printFunctions(F);

  return F;
}

/// Adds memoization code to the delegate function. This includes the check to
/// see if its value has been memoized, and the code to update the memoization
/// cache once invoked.
void ProgramSlice::addMemoizationCode(Function *F, ReturnInst *new_ret) {
  StructType *thunkStructType = getThunkStructType(true);
  IRBuilder<> builder(F->getContext());
  LLVMContext &Ctx = F->getParent()->getContext();

  assert(isa<PointerType>(F->arg_begin()->getType()) &&
         "Memoized function does not have PointerType argument!\n");

  // create new entry block and block to insert memoed return
  BasicBlock *oldEntry = &F->getEntryBlock();
  BasicBlock *newEntry =
      BasicBlock::Create(Ctx, "_wyvern_memo_entry", F, &F->getEntryBlock());
  BasicBlock *memoRetBlock =
      BasicBlock::Create(Ctx, "_wyvern_memo_ret", F, oldEntry);

  // load addresses and values for memo flag and memoed value
  Value *argValue = F->arg_begin();
  builder.SetInsertPoint(newEntry);
  Value *memoedValueGEP = builder.CreateStructGEP(thunkStructType, argValue, 1,
                                                  "_wyvern_memo_val_addr");
  LoadInst *memoedValueLoad =
      builder.CreateLoad(thunkStructType->getStructElementType(1),
                         memoedValueGEP, "_wyvern_memo_val");

  Value *memoFlagGEP = builder.CreateStructGEP(thunkStructType, argValue, 2,
                                               "_wyvern_memo_flag_addr");
  LoadInst *memoFlagLoad =
      builder.CreateLoad(thunkStructType->getStructElementType(2), memoFlagGEP,
                         "_wyvern_memo_flag");

  // add if (memoFlag == true) { return memo_val; }
  Value *toBool = builder.CreateTruncOrBitCast(
      memoFlagLoad, builder.getInt1Ty(), "_wyvern_memo_flag_bool");
  BranchInst *memoCheckBranch =
      builder.CreateCondBr(toBool, memoRetBlock, oldEntry);

  builder.SetInsertPoint(memoRetBlock);
  ReturnInst *memoedValueRet = builder.CreateRet(memoedValueLoad);

  // store computed value and update memoization flag
  builder.SetInsertPoint(new_ret);
  builder.CreateStore(builder.getInt1(1), memoFlagGEP);
  builder.CreateStore(new_ret->getReturnValue(), memoedValueGEP);
}

/// Outlines the given slice into a standalone Function, which
/// encapsulates the computation of the original value in
/// regards to which the slice was created. Adds memoization
/// code so that the function saves its evaluated value and
/// returns it on successive executions.
Function *ProgramSlice::memoizedOutline() {
  StructType *thunkStructType = getThunkStructType(true);
  PointerType *thunkStructPtrType = thunkStructType->getPointerTo();
  FunctionType *delegateFunctionType =
      FunctionType::get(_initial->getType(), {thunkStructPtrType}, false);

  std::random_device rd;
  std::mt19937 mt(rd());
  std::uniform_int_distribution<int64_t> dist(1, 1000000000);
  uint64_t random_num = dist(mt);

  std::string functionName =
      "_wyvern_slice_memo_" +
      _initial->getParent()->getParent()->getName().str() + "_" +
      _initial->getName().str() + std::to_string(random_num);
  Function *F = Function::Create(
      delegateFunctionType, Function::ExternalLinkage, functionName,
      _initial->getParent()->getParent()->getParent());

  F->arg_begin()->setName("_wyvern_thunkptr");

  populateFunctionWithBBs(F);
  populateBBsWithInsts(F);
  reorganizeUses(F);
  rerouteBranches(F);
  ReturnInst *new_ret = addReturnValue(F);
  reorderBlocks(F);
  insertLoadForThunkParams(F, true /*memo*/);
  addMemoizationCode(F, new_ret);

  verifyFunction(*F);
  verifyFunction(*_initial->getParent()->getParent());

  printFunctions(F);

  return F;
}
