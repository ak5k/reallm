add_library(
	reallm OBJECT
	main.cpp
	# Network.cpp
	Node.cpp
	# Data.cpp
	# Element.cpp
	# Llm.cpp
	)

target_link_libraries(reallm common)
