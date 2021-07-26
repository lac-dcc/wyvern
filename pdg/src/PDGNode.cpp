#include "PDGNode.hh"

using namespace llvm;

void pdg::Node::addNeighbor(Node &neighbor, EdgeType edge_type)
{
  if (hasOutNeighborWithEdgeType(neighbor, edge_type))
    return;
  Edge *edge = new Edge(this, &neighbor, edge_type);
  addOutEdge(*edge);
  neighbor.addInEdge(*edge);
}

std::set<pdg::Node *> pdg::Node::getInNeighbors()
{
  std::set<Node *> in_neighbors;
  for (auto edge : _in_edge_set)
  {
    in_neighbors.insert(edge->getSrcNode());
  }
  return in_neighbors;
}

std::set<pdg::Node *> pdg::Node::getInNeighborsWithDepType(pdg::EdgeType edge_type)
{
  std::set<Node *> in_neighbors_with_dep_type;
  for (auto edge : _in_edge_set)
  {
    if (edge->getEdgeType() == edge_type)
      in_neighbors_with_dep_type.insert(edge->getDstNode());
  }
  return in_neighbors_with_dep_type;
}

std::set<pdg::Node *> pdg::Node::getOutNeighbors()
{
  std::set<Node *> out_neighbors;
  for (auto edge : _out_edge_set)
  {
    out_neighbors.insert(edge->getDstNode());
  }
  return out_neighbors;
}

std::set<pdg::Node *> pdg::Node::getOutNeighborsWithDepType(pdg::EdgeType edge_type)
{
  std::set<Node *> out_neighbors_with_dep_type;
  for (auto edge : _out_edge_set)
  {
    if (edge->getEdgeType() == edge_type)
      out_neighbors_with_dep_type.insert(edge->getDstNode());
  }
  return out_neighbors_with_dep_type;
}

bool pdg::Node::hasInNeighborWithEdgeType(Node &n, EdgeType edge_type)
{
  for (auto e : _in_edge_set)
  {
    if (e->getSrcNode() == &n && e->getEdgeType() == edge_type)
      return true;
  }
  return false;
}

bool pdg::Node::hasOutNeighborWithEdgeType(Node &n, EdgeType edge_type)
{
  for (auto e : _out_edge_set)
  {
    if (e->getDstNode() == &n && e->getEdgeType() == edge_type)
      return true;
  }
  return false;
}