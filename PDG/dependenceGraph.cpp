#include "llvm/ADT/Statistic.h"  // For the STATISTIC macro.
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/Constants.h"  // For ConstantData, for instance.
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"  // To use the iterator instructions(f)
#include "llvm/IR/Instructions.h"  // To have access to the Instructions.
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Support/raw_ostream.h"  // For dbgs()
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/IR/DebugInfoMetadata.h" // For DILocation

#include "dependenceGraph.h"

using namespace llvm;

namespace phoenix {

#define TAB std::string("  ")
#define ENDL "\\n"

#define DIGRAPH_BEGIN \
  "digraph G { \n" 
#define DIGRAPH_END \
  "}\n"

#define SUBGRAPH_BEGIN(name) \
  TAB + "subgraph " + QUOTE("cluster_" + name) + " {" \
  + "\n" \
  + TAB + TAB + "label=" + QUOTE(name) + "\n" 
#define SUBGRAPH_END \
  "    color=black\n  }\n"

#define QUOTE(s) \
  "\"" + s + "\""

#define EDGE(id_u, id_v, style, color) \
  TAB + id_u + " -> " + id_v \
  + " [style = " + QUOTE(style) \
  + " color = " + QUOTE(color) + "]"

#define NODE(id, label) \
  TAB + TAB + id \
  + " [label = " + QUOTE(label) + "]"

#define COLORED_NODE(id, label) \
  TAB + TAB + id \
  + " [label = " + QUOTE(label) + " color = \"red\" ]"

#define ID(node) \
  std::to_string(node->id) 

inline std::string bbname(BasicBlock *bb){
  return std::string(bb->getName());
}

void DependenceGraph::add_edge(Value *u, Value *v, DependenceType type) {
  auto &graph = *this;

  auto node_u = new DependenceNode(u, assign_id(u));
  auto node_v = new DependenceNode(v, assign_id(v));
  
  graph[u].insert(new DependenceEdge(node_u, node_v, type));
}

unsigned DependenceGraph::assign_id(Value *v){
  if (ids.find(v) == ids.end())
    ids[v] = ids.size();
  return ids[v];
}

std::map<BasicBlock*, DNSet> DependenceGraph::get_nodes(){
  auto &graph = *this;

  std::map<BasicBlock*, DNSet> m;

  for (auto &kv : graph){
    for (DependenceEdge *edge : kv.second){
      auto *pu = edge->u->parent();
      auto *pv = edge->v->parent();
      
      if (!pu) {
        arg_nodes.insert(edge->u);
      }
      else {
        m[pu].insert(edge->u);
      }

      if (!pv) {
        arg_nodes.insert(edge->v);
      }
      else {
        m[pv].insert(edge->v);
      }
    }
  }

  return m;
}

std::set<DependenceEdge*> DependenceGraph::get_edges(){
  auto &graph = *this;

  std::set<DependenceEdge*> s;

  for (auto &kv : graph){
    for (DependenceEdge *edge : kv.second){
      s.insert(edge);
    }
  }

  return s;
}

std::string DependenceGraph::declare_nodes(std::set<Instruction*> &colored){
  std::string str;
  auto m = get_nodes();

  // node is a DependenceNode
  for (auto &kv : m){

    std::string name = bbname(kv.first);
    str += SUBGRAPH_BEGIN(name);

    auto &s = kv.second;

    for (auto *node : s){
      std::string id = ID(node);
      std::string label = node->label();
      if (isa<Instruction>(node->node) && (colored.count((Instruction*) node->node))) {
          str += COLORED_NODE(id, label) + "\n";
      }
      else {
        str += NODE(id, label) + "\n";
      }
    }

    bool first = true;
    str += TAB + TAB;
    for (auto *node : s){
      std::string id = ID(node);
      if (first)
        str += id;
      else
        str += " -> " + id;
      first = false;
    }

    str += "\n";

    str += SUBGRAPH_END;
  }

  for (auto *node : arg_nodes) {
    std::string id = ID(node);
    std::string label = node->label();
    str += NODE(id, label) + "\n";
  }

  return str;
}

std::string DependenceGraph::declare_edges(){
  std::string str;

  auto s = get_edges();

  for (auto *edge : s){
    std::string id_u = ID(edge->u);
    std::string id_v = ID(edge->v);
    if (edge->type == DT_Data){
      str += EDGE(id_u, id_v, "dashed", "red") + "\n";
    }
    else {
      str += EDGE(id_u, id_v, "dashed", "blue") + "\n";
    }
  }

  return str;
}

void DependenceGraph::to_dot(std::set<Instruction*> &colored){
  std::string str;

  str += DIGRAPH_BEGIN;
  str += declare_nodes(colored);
  str += declare_edges();
  str += DIGRAPH_END;
  errs() << str << "\n";
}

}; // namespace phoenix
