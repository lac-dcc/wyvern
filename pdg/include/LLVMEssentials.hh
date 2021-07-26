#ifndef LLVMESSENTIALS_H_
#define LLVMESSENTIALS_H_
// include the core functionalities needed by all passes
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/Support/CommandLine.h"
#endif