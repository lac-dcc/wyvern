#include "llvm/ADT/Statistic.h"  // For the STATISTIC macro.
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/Constants.h"          // For ConstantData, for instance.
#include "llvm/IR/DebugInfoMetadata.h"  // For DILocation
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"  // To use the iterator instructions(f)
#include "llvm/IR/Instructions.h"  // To have access to the Instructions.
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"  // To print error messages.
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"  // For dbgs()
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <queue>
#include <stack>
#include <vector>

#include "PDGAnalysis.h"
#include "dependenceGraph.h"

using namespace llvm;

#define CONTAINS(v, value) (std::find(v.begin(), v.end(), value) != v.end())

namespace phoenix {

/// \brief We update the predicate iff
///   1. X ends with a conditional jump
///   2. Y does not post-dominates X
///
Value *ProgramDependenceGraph::get_predicate(PostDominatorTree *PDT,
                                             BasicBlock *X,
                                             BasicBlock *Y,
                                             Value *old_pred) {
  auto *ti = X->getTerminator();

  if (!isa<BranchInst>(ti))
    return old_pred;

  BranchInst *br = cast<BranchInst>(ti);

  if (br->isUnconditional() || PDT->properlyDominates(Y, X))
    return old_pred;

  // return br->getCondition();
  return br;
}

// Create control dependence edges for each y \in Y to the predicate *pred
void ProgramDependenceGraph::create_control_edges(BasicBlock *Y, Value *pred) {
  if (pred == nullptr)
    return;

  for (Value &y : *Y) {
    if (Instruction *I = dyn_cast<Instruction>(&y)) {
      //if (I->isTerminator()) {
        DG->add_edge(&y, pred, DT_Control);
      //}
    }
  }
}

void ProgramDependenceGraph::compute_control_dependences(DominatorTree *DT,
                                                         PostDominatorTree *PDT,
                                                         DomTreeNodeBase<BasicBlock> *X,
                                                         Value *pred) {
  create_control_edges(X->getBlock(), pred);

  for (auto *Y : *X) {
    Value *new_pred = get_predicate(PDT, X->getBlock(), Y->getBlock(), pred);
    compute_control_dependences(DT, PDT, Y, new_pred);
  }
  return;
}

std::set<Value *> ProgramDependenceGraph::get_dependences_for(Instruction *start) {
  std::set<Value *> s;
  std::queue<Value *> q;

  s.insert(start);
  q.push(start);

  while (!q.empty()) {
    Value *u = q.front();
    q.pop();

    if (DG->find(u) == DG->end()) {
      continue;
    }

    for (DependenceEdge *edge : DG->operator[](u)) {
      Value *v = edge->v->node;

      if (s.find(v) != s.end())
        continue;

      s.insert(v);
      q.push(v);
    }
  }

  return s;
}

void ProgramDependenceGraph::create_data_edges(Value *start) {
  if (!isa<Instruction>(start))
    return;

  std::set<Value *> s;
  Instruction *I = cast<Instruction>(start);

  for (Use &use : I->operands()) {
    //Track argument data dependencies
    if (Argument *a = dyn_cast<Argument>(use)) {
      DG->add_edge(I, a, DT_Data);
    }

    // only iterate on non-visited instructions
    if (!isa<Instruction>(use) || s.find(use) != s.end())
      continue;

    s.insert(use);

    Instruction *other = cast<Instruction>(use);
    DG->add_edge(I, other, DT_Data);
  }
}

void ProgramDependenceGraph::compute_data_dependences(Function *F) {
  for (BasicBlock &BB : *F) {
    for (Instruction &I : BB) {
      create_data_edges(&I);
    }
  }
}

DependenceGraph *ProgramDependenceGraph::get_dependence_graph() {
  return DG;
}

void ProgramDependenceGraph::compute_dependences(Function *F){
  DominatorTree DT(*F);
  PostDominatorTree PDT;
  PDT.recalculate(*F);
  compute_control_dependences(&DT, &PDT, DT.getRootNode(), nullptr);
  compute_data_dependences(F);
}

ProgramDependenceGraph::ProgramDependenceGraph() {
  this->DG = new DependenceGraph();
}

}  // namespace phoenix