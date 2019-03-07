add_llvm_loadable_module(LLVMWyvern MODULE Wyvern.cpp)

if(APPLE)
	set_target_properties(LLVMWyvern PROPERTIES
	  LINK_FLAGS "-undefined dynamic_lookup"
	)
endif(APPLE)
