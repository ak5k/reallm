add_library(
	reallm OBJECT
	main.cpp
	Llm.cpp
	)

target_link_libraries(reallm common)
