#include "PDGCallGraph.hh"

using namespace llvm;

void pdg::PDGCallGraph::build(Module &M)
{
  for (auto &F : M)
  {
    if (F.isDeclaration() || F.empty())
      continue;
    Node* n = new Node(F, GraphNodeType::FUNC);
    _val_node_map.insert(std::make_pair(&F, n));
    addNode(*n);
  }

  // connect nodes
  for (auto &F : M)
  {
    if (F.isDeclaration() || F.empty())
      continue;
    auto caller_node = getNode(F);
    for (auto inst_i = inst_begin(F); inst_i != inst_end(F); inst_i++)
    {
      if (CallInst *ci = dyn_cast<CallInst>(&*inst_i))
      {
        auto called_func = pdgutils::getCalledFunc(*ci);
        // direct calls
        if (called_func != nullptr)
        {
          auto callee_node = getNode(*called_func);
          if (callee_node != nullptr)
            caller_node->addNeighbor(*callee_node, EdgeType::CONTROLDEP_CALLINV);
        }
        else
        {
          // indirect calls
          auto ind_call_candidates = getIndirectCallCandidates(*ci, M);
          for (auto ind_call_can : ind_call_candidates)
          {
            Node* callee_node = getNode(*ind_call_can);
            if (callee_node != nullptr)
              caller_node->addNeighbor(*callee_node, EdgeType::IND_CALL);
          }
        }
      }
    }
  }
  
  _is_build = true;
}

bool pdg::PDGCallGraph::isFuncSignatureMatch(CallInst &ci, llvm::Function &f)
{
  if (f.isVarArg())
    return false;
  auto actual_arg_list_size = ci.getNumArgOperands();
  auto formal_arg_list_size = f.arg_size();
  if (actual_arg_list_size != formal_arg_list_size)
    return false;
  // compare return type
  auto actual_ret_type = ci.getType();
  auto formal_ret_type = f.getReturnType();
  if (!isTypeEqual(*actual_ret_type, *formal_ret_type))
    return false;
  
  for (unsigned i = 0; i < actual_arg_list_size; i++)
  {
    auto actual_arg = ci.getOperand(i);
    auto formal_arg = f.getArg(i);
    if (!isTypeEqual(*actual_arg->getType(), *formal_arg->getType()))
      return false;
  }
  return true;
}

bool pdg::PDGCallGraph::isTypeEqual(Type& t1, Type &t2)
{
  if (&t1 == &t2)
    return true;
  // need to compare name for sturct, due to llvm-link duplicate struct types
  if (!t1.isPointerTy() || !t2.isPointerTy())
    return false;

  auto t1_pointed_ty = t1.getPointerElementType();
  auto t2_pointed_ty = t2.getPointerElementType();

  if (!t1_pointed_ty->isStructTy() || !t2_pointed_ty->isStructTy())
    return false;
  
  auto t1_name = pdgutils::stripVersionTag(t1_pointed_ty->getStructName().str());
  auto t2_name = pdgutils::stripVersionTag(t2_pointed_ty->getStructName().str());

  return (t1_name == t2_name);
}

std::set<Function *> pdg::PDGCallGraph::getIndirectCallCandidates(CallInst &ci, Module &M)
{
  Type *call_func_ty = ci.getFunctionType();
  assert(call_func_ty != nullptr && "cannot find indirect call for null function type!\n");
  std::set<Function *> ind_call_cand;
  for (auto &F : M)
  {
    if (F.isDeclaration() || F.empty())
      continue;
    if (isFuncSignatureMatch(ci, F))
      ind_call_cand.insert(&F);
  }
  return ind_call_cand;
}

bool pdg::PDGCallGraph::canReach(Node &src, Node &sink)
{
    std::queue<Node*> node_queue;
    std::unordered_set<Node *> seen_node;
    node_queue.push(&src);
    while (!node_queue.empty())
    {
      Node* n = node_queue.front();
      node_queue.pop();
      if (n == &sink)
        return true;
      if (seen_node.find(n) != seen_node.end())
        continue;
      seen_node.insert(n);

      for (auto out_neighbor : n->getOutNeighbors())
      {
        node_queue.push(out_neighbor);
      }
    }
    return false;
}

void pdg::PDGCallGraph::dump()
{
  for (auto pair : _val_node_map)
  {
    if (Function *f = dyn_cast<Function>(pair.first))
    {
      errs() << f->getName() << ": \n";
      for (auto out_node : pair.second->getOutNeighbors())
      {
        if (Function *callee = dyn_cast<Function>(out_node->getValue()))
          errs() << "\t\t" << callee->getName() << "\n";
      }
    }
  }
}

void pdg::PDGCallGraph::printPaths(Node &src, Node &sink)
{
  auto pathes = computePaths(src, sink);
  unsigned count = 1;
  for (auto path : pathes)
  {
    errs() << "************* Printing Pathes **************\n";
    errs() << "path len: " << path.size() << "\n";
    for (auto iter = path.begin(); iter != path.end(); iter++)
    {
      errs() << (*iter)->getName();
      if (std::next(iter, 1) != path.end())
        errs() << " -> ";
      else 
        errs() << "\n\b";
    }
    errs() << "********************************************\n";
    count ++;
  }
}

pdg::PDGCallGraph::PathVecs pdg::PDGCallGraph::computePaths(Node &src, Node &sink)
{
  PathVecs ret;
  std::unordered_set<Function *> visited_funcs;
  bool found_path = false;
  computePathsHelper(ret, src, sink, {}, visited_funcs, found_path); // just find one path
  return ret;
}

void pdg::PDGCallGraph::computePathsHelper(PathVecs &path_vecs, Node &src, Node &sink, std::vector<llvm::Function *> cur_path, std::unordered_set<llvm::Function *> visited_funcs, bool &found_path)
{
  if (found_path)
    return;
  assert(isa<Function>(src.getValue()) && "cannot process non function node (compute path, src)\n");
  assert(isa<Function>(sink.getValue()) && "cannot process non function node (compute path, sink)\n");
  Function* src_func = cast<Function>(src.getValue());
  Function* sink_func = cast<Function>(sink.getValue());
  if (visited_funcs.find(src_func) != visited_funcs.end())
    return;
  visited_funcs.insert(src_func);
  cur_path.push_back(src_func);
  if (src_func == sink_func)
  {
    path_vecs.push_back(cur_path);
    found_path = true;
    return;
  }

  for (auto out_neighbor : src.getOutNeighbors())
  {
    computePathsHelper(path_vecs, *out_neighbor, sink, cur_path, visited_funcs, found_path);
  }
}