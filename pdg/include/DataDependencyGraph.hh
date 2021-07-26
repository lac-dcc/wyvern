#ifndef DATADEPENDENCYGRAPH_H_
#define DATADEPENDENCYGRAPH_H_
#include "Graph.hh"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemoryDependenceAnalysis.h"
#include "llvm/Analysis/MemoryLocation.h"

namespace pdg
{
  class DataDependencyGraph : public llvm::ModulePass
  {
  public:
    static char ID;
    DataDependencyGraph() : llvm::ModulePass(ID) {};
    void getAnalysisUsage(llvm::AnalysisUsage &AU) const override;
    llvm::StringRef getPassName() const override { return "Data Dependency Graph"; }
    bool runOnModule(llvm::Module &M) override;
    void addDefUseEdges(llvm::Instruction &inst);
    void addRAWEdges(llvm::Instruction &inst);
    void addAliasEdges(llvm::Instruction &inst);
    llvm::AliasResult queryAliasUnderApproximate(llvm::Value &v1, llvm::Value &v2);

  private:
    llvm::MemoryDependenceResults *_mem_dep_res;
  };
} // namespace pdg
#endif
