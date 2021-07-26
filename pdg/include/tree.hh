#ifndef TREE_H_
#define TREE_H_
#include "LLVMEssentials.hh"
#include "DebugInfoUtils.hh"
#include "PDGNode.hh"
#include "PDGEnums.hh"
#include "PDGUtils.hh"
#include <set>
#include <unordered_set>

namespace pdg
{
  class Tree;
  class TreeNode : public Node
  {
    public:
      TreeNode(const TreeNode& tree_node); 
      TreeNode(llvm::DIType *di_type, int depth, TreeNode* parent_node, Tree* tree, GraphNodeType node_type);
      TreeNode(llvm::Function &f, llvm::DIType *di_type, int depth, TreeNode* parent_node, Tree* tree, GraphNodeType node_type);
      int expandNode(); // build child nodes and connect with them
      llvm::DILocalVariable *getDILocalVar() { return _di_local_var; }
      void insertChildNode(TreeNode *new_child_node) { _children.push_back(new_child_node); }
      void setParentTreeNode(TreeNode *parent_node) { _parent_node = parent_node; }
      void setDILocalVariable(llvm::DILocalVariable &di_local_var) { _di_local_var = &di_local_var; }
      void addAddrVar(llvm::Value &v) { _addr_vars.insert(&v); }
      std::vector<TreeNode *> &getChildNodes() { return _children; }
      std::unordered_set<llvm::Value *> &getAddrVars() { return _addr_vars; }
      void computeDerivedAddrVarsFromParent();
      TreeNode *getParentNode() { return _parent_node; }
      Tree *getTree() { return _tree; }
      int getDepth() { return _depth; }
      void addAccessTag(AccessTag acc_tag) { _acc_tag_set.insert(acc_tag); }
      std::set<AccessTag> &getAccessTags() { return _acc_tag_set; }
      bool isRootNode() {return _parent_node == nullptr;}
      int numOfChild() { return _children.size(); }
      bool hasReadAccess() { return _acc_tag_set.find(AccessTag::DATA_READ) != _acc_tag_set.end(); }
      bool hasWriteAccess() { return _acc_tag_set.find(AccessTag::DATA_WRITE) != _acc_tag_set.end(); }

    private:
      Tree *_tree;
      TreeNode *_parent_node;
      int _depth;
      llvm::DILocalVariable *_di_local_var;
      std::vector<TreeNode *> _children;
      std::unordered_set<llvm::Value *> _addr_vars;
      std::set<AccessTag> _acc_tag_set;
  };

  class Tree
  {
  public:
    Tree() = default;
    Tree(llvm::Value &v) { _base_val = &v; }
    Tree(const Tree &src_tree);
    void setRootNode(TreeNode &root_node) { _root_node = &root_node; }
    void setTreeNodeType(GraphNodeType node_type) { _root_node->setNodeType(node_type); }
    TreeNode *getRootNode() const { return _root_node; }
    int size() { return _size; }
    void setSize(int size) { _size = size; }
    void increaseTreeSize() { _size++; }
    void print();
    void build(int max_tree_depth = 5);
    llvm::Value *getBaseVal() { return _base_val; }
    void setBaseVal(llvm::Value &v) { _base_val = &v; }

  private:
    llvm::Value* _base_val;
    TreeNode *_root_node;
    int _size;
  };
} // namespace pdg

#endif