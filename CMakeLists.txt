cmake_minimum_required(VERSION 3.20)
project(MysticCrafting LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(IMGUI_SOURCES
	deps/imgui/imgui.cpp
	deps/imgui/imgui_draw.cpp
	deps/imgui/imgui_tables.cpp
	deps/imgui/imgui_widgets.cpp
)

set(MC_SOURCES
	src/Addon.cpp
	src/Core.cpp
	src/Http.cpp
	src/Parse.cpp
	src/Planner.cpp
	src/Store.cpp
	src/Ui.cpp
)

if(WIN32)
	enable_language(RC)
	set(MC_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png")
	set(MC_ICON_HOVER_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon_hover.png")
	configure_file(assets/resources.rc.in ${CMAKE_CURRENT_BINARY_DIR}/resources.rc @ONLY)
	list(APPEND MC_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/resources.rc)

	add_library(MysticCrafting SHARED ${MC_SOURCES} ${IMGUI_SOURCES})

	target_include_directories(MysticCrafting PRIVATE
		src
		deps
		deps/imgui
		deps/nexus
	)

	target_compile_definitions(MysticCrafting PRIVATE
		WIN32_LEAN_AND_MEAN
		NOMINMAX
		_CRT_SECURE_NO_WARNINGS
	)

	target_link_libraries(MysticCrafting PRIVATE winhttp crypt32)

	if(MINGW)
		# Self-contained DLL: no libc++/libunwind/winpthread runtime DLLs required.
		target_link_options(MysticCrafting PRIVATE -static-libgcc -static-libstdc++)
	endif()

	set_target_properties(MysticCrafting PROPERTIES
		OUTPUT_NAME "MysticCrafting"
		PREFIX ""
		SUFFIX ".dll"
	)

	install(TARGETS MysticCrafting RUNTIME DESTINATION .)
endif()

if(NOT WIN32)
	enable_testing()

	add_executable(mc_tests
		tests/test_plan.cpp
		src/Core.cpp
		src/Parse.cpp
		src/Planner.cpp
	)
	target_include_directories(mc_tests PRIVATE src deps)
	add_test(NAME planner COMMAND mc_tests)

	add_executable(mc_ui_tests
		tests/ui_smoke.cpp
		src/Ui.cpp
		src/Core.cpp
		src/Planner.cpp
		${IMGUI_SOURCES}
	)
	target_include_directories(mc_ui_tests PRIVATE tests/stub src deps deps/imgui)
	add_test(NAME ui_smoke COMMAND mc_ui_tests)
endif()
