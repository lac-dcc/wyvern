add_library(Wyvern MODULE
	FindLazyfiable.cpp
	Instrumentation.cpp
	ProgramSlice.cpp
	Lazyfication.cpp
)

target_compile_features(Wyvern PRIVATE cxx_range_for cxx_auto_type)

set_target_properties(Wyvern PROPERTIES
	COMPILE_FLAGS "-fno-rtti"
)


if(APPLE)
	set_target_properties(Wyvern PROPERTIES
		LINK_FLAGS "-undefined dynamic_lookup"
	)
endif(APPLE)
