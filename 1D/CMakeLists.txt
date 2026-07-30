cmake_minimum_required(VERSION 3.16)
project(QuantumWalkSimulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_GUI "Build the raylib GUI application" ON)
option(BUILD_TESTS "Build C++ regression tests" ON)

add_library(sim_engine STATIC
    Simulator2D.cpp
    DataParser.cpp
)
target_include_directories(sim_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Threads REQUIRED)
target_link_libraries(sim_engine PUBLIC Threads::Threads)

add_executable(headless_runner headless_runner.cpp)
target_link_libraries(headless_runner PRIVATE sim_engine)

if(BUILD_GUI)
    find_path(RAYLIB_INCLUDE_DIR raylib.h
        HINTS /opt/homebrew/include /usr/local/include /usr/include)
    find_library(RAYLIB_LIBRARY raylib
        HINTS /opt/homebrew/lib /usr/local/lib /usr/lib)

    if(RAYLIB_INCLUDE_DIR AND RAYLIB_LIBRARY)
        add_library(sim_gui_bits STATIC
            Simulator1D.cpp
            Simulator2DRenderer.cpp
        )
        target_include_directories(sim_gui_bits PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${RAYLIB_INCLUDE_DIR}
        )
        target_link_libraries(sim_gui_bits PUBLIC sim_engine)

        add_executable(diffusion_sim main.cpp)
        target_include_directories(diffusion_sim PRIVATE ${RAYLIB_INCLUDE_DIR})
        target_link_libraries(diffusion_sim PRIVATE sim_gui_bits ${RAYLIB_LIBRARY})

        if(APPLE)
            target_link_libraries(diffusion_sim PRIVATE
                "-framework OpenGL"
                "-framework Cocoa"
                "-framework IOKit"
                "-framework CoreVideo"
            )
        elseif(UNIX)
            target_link_libraries(diffusion_sim PRIVATE m pthread dl GL X11)
        endif()

        message(STATUS "GUI target enabled (raylib found)")
    else()
        message(WARNING "raylib not found; skipping GUI target. Headless runner still builds.")
    endif()
endif()

if(BUILD_TESTS)
    enable_testing()
    add_executable(engine_tests tests/engine_tests.cpp)
    target_link_libraries(engine_tests PRIVATE sim_engine)
    add_test(NAME engine_tests COMMAND engine_tests
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

install(TARGETS headless_runner RUNTIME DESTINATION bin)
