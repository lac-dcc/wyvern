#include "ProgramSlice.h"

#define DEBUG_TYPE "ProgamSlicing"

using namespace llvm;

ProgramSlice::ProgramSlice(Instruction &I, pdg::ProgramGraph *g) {
	std::set<Instruction*> instsInSlice;

	std::stack<pdg::Node *> toVisit;
	pdg::Node* initial = g->getNode((Value&) I);

	toVisit.push(initial);

	while (!toVisit.empty()) {
		pdg::Node* cur = toVisit.top();
		toVisit.pop();
		instsInSlice.insert((Instruction*) cur->getValue());
		for (pdg::Node* node : cur->getInNeighbors()) {
			if (node->getValue() == nullptr) {
				continue;
			}

			if (!isa<Instruction>(node->getValue())) {
				continue;
			}

			Instruction *currentInst = (Instruction*) node->getValue();
			//we do not want interprocedural slices

			if (currentInst->getParent()->getParent() != I.getParent()->getParent()) {
					continue;
			}

			if (instsInSlice.count(currentInst)) {
				continue;
			}

			toVisit.push(node);
		}
	}

	_instsInSlice = instsInSlice;
	_initial = &I;
	_parentFunction = I.getParent()->getParent();
}

void ProgramSlice::insertNewBB(BasicBlock *originalBB, Function *F) {
	auto originalName = originalBB->getName();
	std::string newBBName = "sliceclone_" + originalName.str();
	BasicBlock *newBB = BasicBlock::Create(F->getParent()->getContext(), newBBName, F);
	_origToNewBBmap.insert(std::make_pair(originalBB, newBB));
	_newToOrigBBmap.insert(std::make_pair(newBB, originalBB));
}

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
	}
}

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

void ProgramSlice::reorganizeUses(Function *F) {
	for (auto &pair : _origToNewBBmap) {
		BasicBlock *originalBB = pair.first;
		originalBB->replaceUsesWithIf(pair.second, [F](Use &U) {
			auto *UserI = dyn_cast<Instruction>(U.getUser());
			return UserI && UserI->getParent()->getParent() == F;
		});
		
		for (auto  &pair : _Imap) {
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
}

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

void ProgramSlice::addReturnValue(Function *F) {
	BasicBlock *exit = nullptr;
	for (BasicBlock &BB : *F) {
		if (ReturnInst *RI = dyn_cast<ReturnInst>(BB.getTerminator())) {
			exit = &BB;
		}
	}

	exit->getTerminator()->eraseFromParent();
	ReturnInst *new_ret = ReturnInst::Create(F->getParent()->getContext(), _Imap[_initial], exit);
}

Function *ProgramSlice::outline() {
	Module *M = _initial->getParent()->getParent()->getParent();
	LLVMContext &Ctx = M->getContext();

	FunctionType *FT = FunctionType::get(_initial->getType(), false);
	std::string functionName = "_wyvern_slice_" + _initial->getParent()->getParent()->getName().str() + _initial->getName().str();
	Function *F = Function::Create(FT, Function::ExternalLinkage, functionName, M);

	populateFunctionWithBBs(F);
	populateBBsWithInsts(F);
	addMissingTerminators(F);
	reorganizeUses(F);
	addReturnValue(F);

	verifyFunction(*F);

	return F;
}