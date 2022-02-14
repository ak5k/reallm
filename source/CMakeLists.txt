add_library(
	reallm OBJECT
	main.cpp
	llm.cpp
	)

target_link_libraries(reallm common)
