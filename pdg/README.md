# PDG Document

## Introduction

This project is a key component of our PtrSplit and Program-mandering works. It aims at building a modular inter-procedural program dependence graph (PDG) for practical use. Our program dependence graph is field senstive, context-insensitive and flow-insensitive. For more details, welcome to read our CCS'17 paper about PtrSplit: \[[http://www.cse.psu.edu/~gxt29/papers/ptrsplit.pdf\]](http://www.cse.psu.edu/~gxt29/papers/ptrsplit.pdf%5D) If you find this tool useful, please cite the PtrSplit paper in your publication. Here's the bibtex entry:

@inproceedings{LiuTJ17Ptrsplit,

author = {Shen Liu and Gang Tan and Trent Jaeger},

title = {{PtrSplit}: Supporting General Pointers in Automatic Program Partitioning},

booktitle = {24th ACM Conference on Computer and Communications Security ({CCS})},

pages = {2359--2371},

year = {2017}

}

Currently, the implmentation in master branch is based on LLVM 10.0.1. 

The current implementation only supports building PDGs for C programs.

For detailed description of PDG node and edge, please refer to **PDG_Specification.md**. 


## Getting Started
```
mkdir build
cd build
cmake ..
make
opt -load libpdg.so -dot-pdg < test.bc
```

### Available Passes

**\-pdg:** generate the program dependence graph (inter-procedural)

**\-cdg:** generate the control dependence graph (intra-procedural)

**\-ddg:** generate the data dependence graph (intra-procedural)

**\-dot-\*:** for visualization. (dot)

For those large software, generating a visualizable PDG is not easy. Graphviz often fails to generate the .dot file for a program with more than 1000 lines of C code. Fortunately, we rarely need such a large .dot file but only do kinds of analyses on the PDG, which is always in memory.

## LLVM IR compilation
For simple C programs(e.g., test.c), do

> clang -emit-llvm -S -g test.c -o test.bc

Now you have a binary format LLVM bitcode file which can be directly used as the input for PDG generation.

For those large C software (e.g., wget), you can refer to this great article for help:

[Compiling Autotooled projects to LLVM Bitcode](http://gbalats.github.io/2015/12/10/compiling-autotooled-projects-to-LLVM-bitcode.html)

(We successfully compiled SPECCPU 2006 INT/thttpd/wget/telnet/openssh/curl/nginx/sqlite, thanks to the author!)

## User Guide

We can use the current PDG as a required pass through following steps:

### Compile PDG

1. download PDG repo: git clone https://github.com/ARISTODE/program-dependence-graph.git
2. cd program-dependence-graph
3. make

### Use PDG as a required Pass
Using cmake, add 
```
include_directories(program_dependence_graph/include)
add_subdirectory(program_dependence_graph)
```

Then, add 
```
AU.addRequired<ProgramDependencyGraph>();
```
in your pass's **getAnalysisUsage** method (legacy pass manager).

### Useful APIs

**Query the reachability of two nodes:**

```
ProgramGraph *g = getAnalysis<ProgramDependencyGraph>()->getPDG();

Value* src;
Value* dst;

pdg::Node* src_node = g->getNode(*src);
pdg::Node* dst_node = g->getNode(*dst);

if (g->canReach(src_node, dst_node)) 
{
  // do something...
}

```


**Traverse the PDG with path constrains**
This method is useful to traverse the graph through certain edge types. In the example, we put the edge types we want to exclude in the set **exclude_edges**. Then, pass that as an argument to the **canReach** function.

```
ProgramGraph *g = getAnalysis<ProgramDependencyGraph>()->getPDG();

Value* src;
Value* dst;

pdg::Node* src_node = g->getNode(*src);
pdg::Node* dst_node = g->getNode(*dst);

std::set<pdg::EdgeType> exclude_edges;

if (g->canReach(src_node, dst_node, exclude_edges)) 
{
  // do something...
}
```
