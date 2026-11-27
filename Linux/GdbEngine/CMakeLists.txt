add_library(GdbEngine STATIC
	MiParser.h MiParser.cpp
	GdbProcess.h GdbProcess.cpp
	GdbDebugger.h GdbDebugger.cpp
	EventLoop.h EventLoop.cpp
	Utf8.h)

target_include_directories(GdbEngine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(GdbEngine PUBLIC DebugEngineInterface Threads::Threads)
target_compile_options(GdbEngine PRIVATE -Wall -Wextra -Wno-unused-parameter)
