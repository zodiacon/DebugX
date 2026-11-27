# the programs the tests debug: unoptimized, with full debug information (the tests depend on their lines, by @markers)
foreach(target loop values crash threads)
	add_executable(${target} Targets/${target}.cpp)
	target_compile_options(${target} PRIVATE -O0 -g3 -fno-omit-frame-pointer)
	target_link_libraries(${target} PRIVATE Threads::Threads)
	set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/targets)
endforeach()

add_executable(GdbEngineTests GdbEngineTests.cpp)
target_link_libraries(GdbEngineTests PRIVATE GdbEngine)
target_compile_definitions(GdbEngineTests PRIVATE
	TARGETS_DIR="${CMAKE_BINARY_DIR}/targets"
	TARGET_SOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Targets")
add_dependencies(GdbEngineTests loop values crash threads)

add_test(NAME GdbEngineTests COMMAND GdbEngineTests)
