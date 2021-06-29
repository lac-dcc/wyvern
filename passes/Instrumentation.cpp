#include "FindLazyfiable.h"
#include "Instrumentation.h"

using namespace llvm;

#define DEBUG_TYPE "WyvernInstrumentationPass"

void WyvernInstrumentationPass::InstrumentExit(Function *F, long long func_id, AllocaInst* bits) {
	LLVMContext& Ctx = F->getParent()->getContext();
	int num_args = F->arg_size();

	ConstantInt* numArgs = ConstantInt::get(Ctx, llvm::APInt(32, num_args, true));
	ConstantInt* funcId = ConstantInt::get(Ctx, llvm::APInt(64, func_id, true));
	for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
		if (auto *RI = dyn_cast<ReturnInst>(&*I)) {
			IRBuilder<> builder(RI);
			Value* args[] = { bits, numArgs, funcId };
			builder.CreateCall(logFun, args);
		}
	}
}

AllocaInst* WyvernInstrumentationPass::InstrumentEntry(Function *F) {
	BasicBlock& entry = F->getEntryBlock();
	LLVMContext& Ctx = F->getParent()->getContext();

	Instruction& firstInst = *entry.begin();
	IRBuilder<> builder(&firstInst);
	AllocaInst* alloc = builder.CreateAlloca(Type::getInt64Ty(Ctx));
	CallInst* call = builder.CreateCall(initBitsFun);
	builder.CreateStore(call, alloc);
	Value* args2[] = {};

	return alloc;
}
	
void WyvernInstrumentationPass::InstrumentFunction(Function *F, long long func_id) {
	AllocaInst* usedBits = InstrumentEntry(F);
	InstrumentExit(F, func_id, usedBits);

	std::map<Value*, int> vArgs;
	int index = 0;
	for (auto &arg : F->args()) {
		if (Value *vArg = dyn_cast<Value>(&arg)) {
			vArgs[vArg] = index;
		}
		++index;
	}

	inst_iterator I = inst_begin(F);
	for (inst_iterator E = inst_end(F); I != E; ++I) {
		for (Use &U : I->operands()) {
			if (auto *V = dyn_cast<Value>(&U)) {
				if (vArgs.count(V) > 0) {
					IRBuilder<> builder(&*I);

					if (isa<PHINode>(&*I)) {
						BasicBlock *BB = I->getParent();
						builder.SetInsertPoint(BB, BB->getFirstInsertionPt());
					} 

					ConstantInt* argIndex = ConstantInt::get(F->getParent()->getContext(), llvm::APInt(32, vArgs[V], true));
					Value* args[] = { usedBits, argIndex };
					builder.CreateCall(markFun, args);
				}
			}
		}
	}	
}

void removeDummyFunctions(std::set<Function*> dummyFunctions) {
	for (auto &F : dummyFunctions) {
		for (auto User : F->users()) {
			CallInst* dummyFunCall = (CallInst*) User;
			dummyFunCall->eraseFromParent();
		}
		F->eraseFromParent();
	}
}

FunctionCallee getDummyFunctionForType(Module &M, Type* Type) {
	LLVMContext& Ctx = M.getContext();
	std::string typeName;
	raw_string_ostream typeNameOs(typeName);
	Type->print(typeNameOs);
	std::string dummyFunctionName = "dummy_fun" + typeNameOs.str();
	FunctionCallee dummyFunction = M.getOrInsertFunction(dummyFunctionName, Type::getVoidTy(Ctx), Type);
	return dummyFunction;
}

std::set<Function*> WyvernInstrumentationPass::addMissingUses(Module &M, LLVMContext& Ctx) {
	std::set<Function*> dummyFunctions;
	for (Function &F : M) {
		std::set<Value*> vArgs;
		for (auto &arg : F.args()) {
			if (Value* vArg = dyn_cast<Value>(&arg)) {
				vArgs.insert(vArg);
			}	
		}

		for (auto I = inst_begin(F), E = inst_end(F); I != E; ++I) {
			if (PHINode *PN = dyn_cast<PHINode>(&*I)) {
				for (auto &value : PN->operands()) {
					if (!vArgs.count(value)) {
						continue;
					}

					FunctionCallee dummyFunction = getDummyFunctionForType(M, value->getType());
					dummyFunctions.insert((Function *) dummyFunction.getCallee());
					Value* args[] = { value };
					BasicBlock* incBlock = PN->getIncomingBlock(value);
					IRBuilder<> builder(incBlock, --incBlock->end());
					builder.CreateCall(dummyFunction, args);
				}
			}
		}
	}
	return dummyFunctions;
}

void WyvernInstrumentationPass::InstrumentExitPoints(Module &M, Value* num_funcs_arg) {
	LLVMContext& Ctx = M.getContext();
	Value* args[] = { num_funcs_arg };

	for (Function &F : M) {
		for (inst_iterator I = inst_begin(F); I != inst_end(F); ++I) {
			if (F.getName() == "main") {
				if (auto *RI = dyn_cast<ReturnInst>(&*I)) {
					IRBuilder<> builder(RI);
					builder.CreateCall(dumpFun, args);
				}
			}

			if (auto *CI = dyn_cast<CallInst>(&*I)) {
				if (CI->getCalledFunction() &&
					(CI->getCalledFunction()->getName() == "exit" ||
					CI->getCalledFunction()->getName() == "abort")) {
					IRBuilder<> builder(CI);
					builder.CreateCall(dumpFun, args);
				}
			}
		}
	}
}

bool WyvernInstrumentationPass::runOnModule(Module &M) {
	LLVMContext& Ctx = M.getContext();

	initBitsFun = M.getOrInsertFunction("_wyinstr_initbits", Type::getInt64Ty(Ctx));
	markFun = M.getOrInsertFunction("_wyinstr_mark", Type::getVoidTy(Ctx), Type::getInt64Ty(Ctx)->getPointerTo(), Type::getInt32Ty(Ctx));
	dumpFun = M.getOrInsertFunction("_wyinstr_dump", Type::getVoidTy(Ctx), Type::getInt32Ty(Ctx));
	logFun = M.getOrInsertFunction("_wyinstr_log_func", Type::getVoidTy(Ctx), Type::getInt64Ty(Ctx)->getPointerTo(), Type::getInt32Ty(Ctx), Type::getInt64Ty(Ctx));

	std::set<Function*> dummyFunctions = addMissingUses(M, Ctx);

	FindLazyfiableAnalysis &FLA = getAnalysis<FindLazyfiableAnalysis>();

	std::error_code ec;
	raw_fd_ostream outfile("function_ids.csv", ec);
	outfile << "function,id\n";
	long long func_id = 0;
	for (auto const &F : FLA.lazyFunctions) {
		outfile << F->getName() << "," << func_id << "\n";
		InstrumentFunction(F, func_id++);
	}

	int num_instrumented_funcs = FLA.lazyFunctions.size();
	ConstantInt* num_funcs_arg = ConstantInt::get(Ctx, llvm::APInt(32, num_instrumented_funcs, true));
	InstrumentExitPoints(M, num_funcs_arg);

	removeDummyFunctions(dummyFunctions);

	return true;
}

void WyvernInstrumentationPass::getAnalysisUsage(AnalysisUsage &AU) const {
	AU.addRequired<FindLazyfiableAnalysis>();
}

char WyvernInstrumentationPass::ID = 0;
static RegisterPass<WyvernInstrumentationPass> X("instrument", "Instrument functions to track argument usage.", true, true);
