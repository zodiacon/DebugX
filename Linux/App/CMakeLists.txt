find_package(Qt6 QUIET COMPONENTS Widgets)
if(NOT Qt6_FOUND)
	message(STATUS "Qt 6 (Widgets) not found: the GUI (debugx) is not built")
	return()
endif()

# QScintilla (Debian/Ubuntu: libqscintilla2-qt6-dev): the source documents and the Disassembly pane; it has no CMake
# package, so its header and library are looked for next to Qt's
find_path(QSCINTILLA_INCLUDE_DIR Qsci/qsciscintilla.h
	HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_HEADERS}" PATH_SUFFIXES qt6 qt6/include)
find_library(QSCINTILLA_LIBRARY NAMES qscintilla2_qt6 HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_LIBS}")
if(NOT QSCINTILLA_INCLUDE_DIR OR NOT QSCINTILLA_LIBRARY)
	message(STATUS "QScintilla for Qt 6 not found (libqscintilla2-qt6-dev): the GUI (debugx) is not built")
	return()
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

qt_add_executable(debugx
	main.cpp
	Common.h Common.cpp
	QtDispatcher.h
	DebugSession.h DebugSession.cpp
	PaneBase.h PaneBase.cpp
	MainWindow.h MainWindow.cpp
	CommandView.h CommandView.cpp
	OutputView.h OutputView.cpp
	ListPanes.h ListPanes.cpp
	SymbolPanes.h SymbolPanes.cpp
	DisassemblyView.h DisassemblyView.cpp
	MemoryView.h MemoryView.cpp
	CodeEditor.h CodeEditor.cpp
	Dialogs.h Dialogs.cpp
	# shared with the Windows GUI
	../../DebugX/SessionModel.h ../../DebugX/SessionModel.cpp)

target_include_directories(debugx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ../../DebugX ${QSCINTILLA_INCLUDE_DIR})
target_link_libraries(debugx PRIVATE GdbEngine Qt6::Widgets ${QSCINTILLA_LIBRARY})
target_compile_options(debugx PRIVATE -Wall -Wno-unused-parameter)
set_target_properties(debugx PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
