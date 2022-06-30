#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"

#include <string>
#include <vector>

namespace llvm {

void generatePrintf(std::string_view fmt,
                           const std::vector<Value *> &args,
                           IRBuilder<> &builder);
} // namespace llvm