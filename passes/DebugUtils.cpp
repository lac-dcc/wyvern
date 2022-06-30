#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"

#include <string>
#include <vector>

namespace llvm {

void generatePrintf(std::string_view fmt,
                           const std::vector<llvm::Value *> &args,
                           llvm::IRBuilder<> &builder) {
  Module *M = builder.GetInsertBlock()->getParent()->getParent();

  Type *i32 = builder.getInt32Ty();
  PointerType *i8 = builder.getInt8PtrTy();
  ConstantPointerNull *nullPtr = ConstantPointerNull::get(i8);
  FunctionType *printfType = FunctionType::get(i32, true);
  FunctionCallee printfFunction = M->getOrInsertFunction("printf", printfType);
  FunctionType *fflushType = FunctionType::get(i32, {i8}, false);
  FunctionCallee fflushFunction = M->getOrInsertFunction("fflush", fflushType);

  Constant *ptr = builder.CreateGlobalStringPtr(fmt);

  std::vector<llvm::Value *> args_ = {ptr};
  for (auto *arg : args) {
    args_.emplace_back(arg);
  }

  builder.CreateCall(printfFunction, args_, "_wyvern_debug_print");
  builder.CreateCall(fflushFunction, {nullPtr}, "_wyvern_debug_fflush");
}

} // namespace llvm