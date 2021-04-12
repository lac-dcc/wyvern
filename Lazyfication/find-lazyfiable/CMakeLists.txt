add_library(FindLazyfiablePass MODULE
	FindLazyfiable.cpp
)

target_compile_features(FindLazyfiablePass PRIVATE cxx_range_for cxx_auto_type)

set_target_properties(FindLazyfiablePass PROPERTIES
	COMPILE_FLAGS "-fno-rtti"
)

if(APPLE)
	set_target_properties(FindLazyfiablePass PROPERTIES
		LINK_FLAGS "-undefined dynamic_lookup"
	)
endif(APPLE)
