#include "Wyvern.h"

using namespace llvm;

char Wyvern::ID = 0;

namespace {

static RegisterPass<Wyvern> X("wyvern", "Wyvern Pass",
                             false,
                             false);

}

Wyvern::Wyvern() : FunctionPass(Wyvern::ID) {}

bool Wyvern::runOnFunction(Function &F) {
  errs() << "Hello: ";
  errs().write_escaped(F.getName()) << '\n';
  errs() << "Testing!\n";

  PostDominatorTree &PDT = 
                 getAnalysis<PostDominatorTreeWrapperPass>().getPostDomTree();

  const BasicBlock &entryBB = F.getEntryBlock();
  errs() << "Entry: ";
  entryBB.dump();

  auto entryNode = PDT.getNode(&entryBB);

  for (auto it = F.arg_begin(); it != F.arg_end(); ++it) {
    errs() << "Arg: ";
    it->dump();
	for (auto it2 = it->use_begin(); it2 != it->use_end(); ++it2) {
      errs() << "Use: ";
      Value *user = it2->getUser();
      if (auto *I = dyn_cast<Instruction>(user)) {
        BasicBlock *useBB = I->getParent();
        errs() << "UseBB: ";
        useBB->dump();
        auto useNode = PDT.getNode(useBB);

        if(PDT.properlyDominates(useNode, entryNode)) {
          errs() << "PostDominates!\n";
        }
        else {
          errs() << "Does not PostDominate!\n";
        }
      }
    }
  }

  return false;
}

void Wyvern::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  AU.addRequired<PostDominatorTreeWrapperPass>();
}
