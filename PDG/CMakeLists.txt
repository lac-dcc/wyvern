add_library(PDG MODULE
	dependenceGraph.cpp
	PDGAnalysis.cpp
)

target_compile_features(PDG PRIVATE cxx_range_for cxx_auto_type)

set_target_properties(PDG PROPERTIES
	COMPILE_FLAGS "-fno-rtti"
)


if(APPLE)
	set_target_properties(PDG PROPERTIES
		LINK_FLAGS "-undefined dynamic_lookup"
	)
endif(APPLE)
