#include "DebugInfoUtils.hh"

using namespace llvm;

// ===== check types =====
bool pdg::dbgutils::isPointerType(DIType &dt)
{
  return (dt.getTag() == dwarf::DW_TAG_pointer_type);
}

bool pdg::dbgutils::isStructType(DIType &dt)
{
  return (dt.getTag() == dwarf::DW_TAG_structure_type);
}

bool pdg::dbgutils::isUnionType(DIType& dt)
{
  return (dt.getTag() == dwarf::DW_TAG_union_type);
}

bool pdg::dbgutils::isStructPointerType(DIType &dt)
{
  if (isPointerType(dt))
  {
    DIType *lowest_di_type = getLowestDIType(dt);
    if (lowest_di_type == nullptr)
      return false;
    if (isStructType(*lowest_di_type))
      return true;
  }
  return false;
}

bool pdg::dbgutils::isFuncPointerType(DIType &dt)
{
  DIType* di = stripMemberTag(dt);
  if (di->getTag() == dwarf::DW_TAG_subroutine_type || isa<DISubroutineType>(di) || isa<DISubprogram>(di))
    return true;
  auto lowest_di_type = getLowestDIType(*di);
  if (lowest_di_type != nullptr)
    return (lowest_di_type->getTag() == dwarf::DW_TAG_subroutine_type) || isa<DISubroutineType>(lowest_di_type) || isa<DISubprogram>(lowest_di_type);
  return false;
}

bool pdg::dbgutils::isProjectableType(DIType &dt)
{
  return (isStructType(dt) || isUnionType(dt));
}

bool pdg::dbgutils::hasSameDIName(DIType &d1, DIType &d2)
{
  std::string d1_name = dbgutils::getSourceLevelTypeName(d1);
  std::string d2_name = dbgutils::getSourceLevelTypeName(d2);
  return (d1_name == d2_name);
}

// ===== derived types related operations =====
DIType *pdg::dbgutils::getBaseDIType(DIType &dt)
{
  if (DIDerivedType *derived_ty = dyn_cast<DIDerivedType>(&dt))
    return derived_ty->getBaseType();
  return nullptr;
}

DIType *pdg::dbgutils::getLowestDIType(DIType &dt)
{
  DIType *current_dt = &dt;
  if (!current_dt)
    return nullptr;
  while (DIDerivedType *derived_dt = dyn_cast<DIDerivedType>(current_dt))
  {
    current_dt = derived_dt->getBaseType();
    if (!current_dt) // could happen for a pointer to void pointer etc
      break;
  }
  return current_dt;
}

DIType *pdg::dbgutils::stripAttributes(DIType &dt)
{
  DIType *current_dt = &dt;
  assert(current_dt != nullptr && "cannot strip attr on null di type!");
  auto type_tag = dt.getTag();
  while (type_tag == dwarf::DW_TAG_typedef ||
         type_tag == dwarf::DW_TAG_const_type ||
         type_tag == dwarf::DW_TAG_volatile_type)
  {
    if (DIDerivedType *didt = dyn_cast<DIDerivedType>(current_dt))
    {
      DIType *baseTy = didt->getBaseType();
      if (baseTy == nullptr)
        return nullptr;
      current_dt = baseTy;
      type_tag = current_dt->getTag();
    }
  }
  return current_dt;
}

DIType *pdg::dbgutils::stripMemberTag(DIType &dt)
{
  auto type_tag = dt.getTag();
  if (type_tag == dwarf::DW_TAG_member)
    return getBaseDIType(dt);
  return &dt;
}

// ===== get the source level naming information for variable or types ===== 
std::string pdg::dbgutils::getSourceLevelVariableName(DINode &di_node)
{
  if (DILocalVariable *di_var = dyn_cast<DILocalVariable>(&di_node))
  {
    return di_var->getName().str();
  }

  // get field name
  if (DIType *dt = dyn_cast<DIType>(&di_node))
  {
    auto type_tag = dt->getTag();
    switch (type_tag)
    {
      case dwarf::DW_TAG_member:
      {
        return dt->getName().str();
      }
      case dwarf::DW_TAG_structure_type:
        return dt->getName().str();
      case dwarf::DW_TAG_typedef:
        return dt->getName().str();
      default:
        return dt->getName().str();
    }
  }
  return "";
}

std::string pdg::dbgutils::getSourceLevelTypeName(DIType &dt)
{
  auto type_tag = dt.getTag();
  if (!type_tag)
    return "";
  switch (type_tag)
  {
  case dwarf::DW_TAG_pointer_type:
  {
    // errs() << "1\n";
    auto base_type = getBaseDIType(dt);
    if (!base_type)
      return "nullptr";
    return getSourceLevelTypeName(*base_type) + "*";
  }
  case dwarf::DW_TAG_member:
  {
    // errs() << "2\n";
    auto base_type = getBaseDIType(dt);
    if (!base_type)
      return "null";
    std::string base_type_name = getSourceLevelTypeName(*base_type);
    if (base_type_name == "struct")
      base_type_name = "struct " + dt.getName().str();
    return base_type_name;
  }
  // assert(!type_name.empty() && !var_name.empty() && "cannot generation idl from empty var/type name!");
  case dwarf::DW_TAG_structure_type:
  {
    // errs() << "3\n";
    if (dt.getName().empty())
      return "struct";
    return "struct " + dt.getName().str();
  }
  case dwarf::DW_TAG_array_type:
  {
    // DICompositeType *dct = cast<DICompositeType>(&dt);
    // auto elements = dct->getElements();
    // if (elements.size() == 1)
    // {
    //   // TODO: compute the array size based on new api
    //   DISubrange *di_subr = cast<DISubrange>(elements[0]);
    //   int count = di_subr->getCount().first->getSExtValue();
    //   auto lowest_di_type = getLowestDIType(dt);
    //   if (!lowest_di_type) 
    //     return "";
    //   std::string base_type_name = getSourceLevelTypeName(*lowest_di_type);
    //   return "array<" + base_type_name + ", " + "0" + ">";
    // }
    return "array";
  }
  case dwarf::DW_TAG_const_type:
  {
    // errs() << "4" << "\n";
    auto base_type = getBaseDIType(dt);
    if (!base_type)
      return "const nullptr";
    return "const " + getSourceLevelTypeName(*base_type);
  }
  default:
  {
    // errs() << "5\n";
    return dt.getName().str();
  }
  }
  return "";
}

// compute di type for value
DIType *pdg::dbgutils::getGlobalVarDIType(GlobalVariable &gv)
{
  SmallVector<DIGlobalVariableExpression *, 5> GVs;
  gv.getDebugInfo(GVs);
  if (GVs.size() == 0)
    return nullptr;
  for (auto GV : GVs)
  {
    DIGlobalVariable *digv = GV->getVariable();
    return digv->getType();
  }
  return nullptr;
}

DIType *pdg::dbgutils::getFuncRetDIType(Function &F)
{
  SmallVector<std::pair<unsigned, MDNode *>, 4> MDs;
  F.getAllMetadata(MDs);
  for (auto &MD : MDs)
  {
    MDNode *N = MD.second;
    if (DISubprogram *subprogram = dyn_cast<DISubprogram>(N))
    {
      auto *sub_routine = subprogram->getType();
      const auto &type_ref = sub_routine->getTypeArray();
      if (F.arg_size() >= type_ref.size())
        break;
      // const auto &ArgTypeRef = TypeRef[0];
      // DIType *Ty = ArgTypeRef->resolve();
      // return Ty;
      return type_ref[0];
    }
  }
  return nullptr;
}

std::set<DIType *> pdg::dbgutils::computeContainedStructTypes(DIType &dt)
{
  std::set<DIType* > contained_struct_di_types;
  if (!isStructType(dt))
    return contained_struct_di_types;
  std::queue<DIType*> type_queue;
  type_queue.push(&dt);
  int current_tree_height = 0;
  int max_tree_height = 5;
  while (current_tree_height < max_tree_height)
  {
    current_tree_height++;
    int queue_size = type_queue.size();
    while (queue_size > 0)
    {
      queue_size--;
      DIType *current_di_type = type_queue.front();
      type_queue.pop();
      if (!isStructType(*current_di_type))
        continue;
      if (contained_struct_di_types.find(current_di_type) != contained_struct_di_types.end())
        continue;
      if (getSourceLevelTypeName(*current_di_type).compare("struct") == 0) // ignore anonymous struct
        continue;
      contained_struct_di_types.insert(current_di_type);
      auto di_node_arr = dyn_cast<DICompositeType>(current_di_type)->getElements();
      for (unsigned i = 0; i < di_node_arr.size(); i++)
      {
        DIType *field_di_type = dyn_cast<DIType>(di_node_arr[i]);
        DIType* field_lowest_di_type = getLowestDIType(*field_di_type);
        if (!field_lowest_di_type)
          continue;
        if (isStructType(*field_lowest_di_type))
          type_queue.push(field_lowest_di_type);
      }
    }
  }
  return contained_struct_di_types;
}