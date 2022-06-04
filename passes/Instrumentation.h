#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

namespace llvm {
struct WyvernInstrumentationPass : public ModulePass {
  static char ID;
  WyvernInstrumentationPass() : ModulePass(ID) {}

  /// We store pointers to the instrumentation functions since they'll be reused
  /// often.

  /// The _wyinstr_initprof() function, which initializes the data structures
  /// used by the instrumentation. It is inserted at the program's entry point
  /// (beginning of the main function)
  FunctionCallee initProfFun;

  /// The _wyinstr_initbits() function. It returns a zeroed int64_t value. Used
  /// to initialize the bitmap that tracks argument usage for each function
  /// call.
  FunctionCallee initBitsFun;

  /// The _wyinstr_mark_eval(int8_t arg_index, int64_t *bits) function. It
  /// updates the bitmap in bits to track that argument with index arg_index was
  /// evaluated.
  FunctionCallee markFun;

  /// The _wyinstr_dump() function. Called at the exit points of the program to
  /// dump the results of the profiling.
  FunctionCallee dumpFun;

  /// The _wyinstr_init_call(char *fun_name, int64_t call_id, int8_t num_args)
  /// function. It is inserted before each callsite, and initializes the data
  /// structures to track profiling data for that callsite (if necessary), as
  /// well as update the number of times the callsite has been called.
  FunctionCallee initCallFun;

  /// The _wyinstr_end_call() function. It is inserted at the exit point of
  /// every instrumented function, and updates the shadow call stack to reflect
  /// that the function has returned.
  FunctionCallee endCallFun;

  /// Instruments the program's entry point, with a function to initialize the
  /// instrumentation data structures.
  void InstrumentEntryPoint(Module &M);

  /// Instruments the exit points of the program, to dump profiling results.
  void InstrumentExitPoints(Module &M);

  /// Instruments a given function, inserting calls to mark parameter
  /// evaluations.
  void
  InstrumentFunction(Function *F, std::map<Instruction *, int64_t> instr_ids,
                     std::shared_ptr<std::set<Function *>> promising = nullptr);

  /// Instruments callsites found in the function, to add calls to functions
  /// that track active callsites.
  void InstrumentCallSites(
      Function *F, std::map<Instruction *, int64_t> instr_ids,
      std::shared_ptr<std::set<Function *>> promising = nullptr);

  /// Instruments the entry point of the given function, to initialize the
  /// bitmap of evaluated arguments. Returns the AllocaInst that contains the
  /// memory address of the bitmap.
  AllocaInst *InstrumentEntry(Function *F);
  void getAnalysisUsage(AnalysisUsage &AU) const;
  bool runOnModule(Module &);
};
} // namespace llvm
