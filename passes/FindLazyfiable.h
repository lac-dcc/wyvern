//===- FindLazyfiable.h - Finds call sites candidates for lazification`----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// TODO: check this comment:
/// This file provides the interface for the analysis that finds calls sites
/// that are good candidates to be lazified. A good candidate for lazification
/// is a call site that calls a "promising" function. A function is deemed
/// promising if it contains a path from its entry node until an exit point
/// that does not cross any use of at least one of its formal parameters.
///
//===----------------------------------------------------------------------===//
#include <map>
#include <set>
#include <stack>
#include <utility>

#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
struct FindLazyfiableAnalysis : public ModulePass {
public:
  static char ID;
  FindLazyfiableAnalysis() : ModulePass(ID) {}

  bool runOnModule(Module &);
  void getAnalysisUsage(AnalysisUsage &) const;

  /// Returns the set of promising functions, regardless of which formal
  /// paremeter happens to be lazifiable. Used for instrumentation.
  const std::set<Function *> &getPromisingFunctions() {
    return _promisingFunctions;
  }

  /// Returns the set of (promising_function, promising_argument) pair. Each pair
  /// represents a pair of a promising function, plus the index of its
  /// formal parameter that is potentially unused.
  const std::set<std::pair<Function *, int>> &getPromisingFunctionArgs() {
    return _promisingFunctionArgs;
  }

  /// Returns the set of (call, argument) lazifiable callsites. Each pair is a
  /// call instruction, plus the index of its lazifiable actual parameter.
  const std::set<std::pair<CallInst *, int>> &getLazyfiableCallSites() {
    return _lazyfiableCallSites;
  }

private:
  /// Stores the set of promising functions found in the program. Used for instrumentation.
  std::set<Function *> _promisingFunctions;

  /// Stores the pairs of (promising_function, promising_parameter) instances.
  std::set<std::pair<Function *, int>> _promisingFunctionArgs;

  /// Stores the pairs of (callsite, lazifiable_argument) instances.
  std::set<std::pair<CallInst *, int>> _lazyfiableCallSites;

  /// Stores the number of (callsite, lazifiable_argument) occurrences, used
  std::set<std::pair<Function *, int>> _lazyfiableCallSitesStats;

  /**
   * Traverses the module @param M, adding explicit uses of
   * values which are used in PHINodes. This ensures implicit
   * value uses (which are only "visible" in control flow) are
   * properly tracked when finding lazyfiable paths.
   *
   */
  std::set<Function *> addMissingUses(Module &M, LLVMContext &Ctx);

  /**
   * Performs a Depth-First Search over a function's CFG, attempting
   * to find paths from entry BB @param first to exit BB @param exit
   * which do not go through any use of argument @param arg.
   *
   * If any such path is found, record them in the analysis' results
   * and statistics.
   *
   */
  void DFS(BasicBlock *, BasicBlock *, std::set<BasicBlock *> &, Value *, int);

  /**
   * Searches for lazyfiable paths in function @param F, by
   * checking whether there are paths in its CFG which do not
   * use each of its input arguments.
   *
   */
  void findLazyfiablePaths(Function &);

  /**
   * Placeholder.
   * Eventually, should be a function which uses a heuristic to try and
   * estimate statically whether the computation of value @param I is worth
   * lazifying.
   */
  bool isArgumentComplex(Instruction &);

  /**
   * Analyzes a given function callsite @param CI, to evaluate whether
   * any of its arguments can/should be encapsulated into a lazyfied
   * lambda/sliced function.
   *
   */
  void analyzeCall(CallInst *);

  /**
   * Dumps statistics for number of lazyfiable call sites and
   * lazyfiable function paths found within the module.
   *
   */
  void dump_results();
};
} // namespace llvm
