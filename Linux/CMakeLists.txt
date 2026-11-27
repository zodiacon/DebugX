cmake_minimum_required(VERSION 3.21)

# DebugX on Linux: the GDB engine behind DebugX's IDebugger (../DebugEngine/IDebugger.h), its tests and the Qt GUI (App: debugx).
# Open this file (or the folder, for CMakePresets.json) in Qt Creator -- not the repository's top-level CMakeLists.txt.
project(DebugXLinux VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(WIN32)
	message(FATAL_ERROR "This is the Linux build; on Windows build DebugX.sln")
endif()

find_package(Threads REQUIRED)

# the engine-neutral interface DebugX's GUI uses, shared with the Windows build
add_library(DebugEngineInterface INTERFACE)
target_include_directories(DebugEngineInterface INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../DebugEngine)
target_sources(DebugEngineInterface INTERFACE
	FILE_SET HEADERS BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/../DebugEngine FILES
		../DebugEngine/IDebugger.h
		../DebugEngine/AsyncTask.h
		../DebugEngine/Dispatcher.h
		../DebugEngine/PlatformTypes.h)

add_subdirectory(GdbEngine)

option(DEBUGX_BUILD_GUI "Build the Qt GUI (debugx)" ON)
if(DEBUGX_BUILD_GUI)
	add_subdirectory(App)
endif()

option(DEBUGX_BUILD_TESTS "Build GdbEngineTests and the programs it debugs" ON)
if(DEBUGX_BUILD_TESTS)
	enable_testing()
	add_subdirectory(Tests)
endif()
