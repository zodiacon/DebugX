# the programs the tests debug: unoptimized, with full debug information (the tests depend on their lines, by @markers)
foreach(target loop values crash threads)
	add_executable(${target} Targets/${target}.cpp)
	target_compile_options(${target} PRIVATE -O0 -g3 -fno-omit-frame-pointer)
	target_link_libraries(${target} PRIVATE Threads::Threads)
	set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/targets)
endforeach()
# and one without any symbols (no main: the launch stops at the first instruction instead)
add_executable(loop-stripped Targets/loop.cpp)
target_compile_options(loop-stripped PRIVATE -O0)
target_link_options(loop-stripped PRIVATE -s)
target_link_libraries(loop-stripped PRIVATE Threads::Threads)
set_target_properties(loop-stripped PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/targets)

add_executable(GdbEngineTests GdbEngineTests.cpp)
target_link_libraries(GdbEngineTests PRIVATE GdbEngine)
target_compile_definitions(GdbEngineTests PRIVATE
	TARGETS_DIR="${CMAKE_BINARY_DIR}/targets"
	TARGET_SOURCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Targets")
add_dependencies(GdbEngineTests loop values crash threads loop-stripped)

add_test(NAME GdbEngineTests COMMAND GdbEngineTests)
