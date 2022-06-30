add_library(Wyvern MODULE
	FindLazyfiable.cpp
	Instrumentation.cpp
	ProgramSlice.cpp
	Lazyfication.cpp
	DebugUtils.cpp
)

target_compile_features(Wyvern PRIVATE cxx_std_17)

set_target_properties(Wyvern PROPERTIES
	COMPILE_FLAGS "-fno-rtti -g -O3"
)

if(APPLE)
	set_target_properties(Wyvern PROPERTIES
		LINK_FLAGS "-undefined dynamic_lookup"
	)
endif(APPLE)
