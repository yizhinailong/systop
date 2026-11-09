cmake_minimum_required(VERSION 4.0.0)
project(
    systop
    LANGUAGES CXX
    VERSION 0.1.0
)

set(CMAKE_CXX_STANDARD 23)

include(FetchContent)
FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG v7.0.3
)
FetchContent_MakeAvailable(ftxui)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/monitor_state.cpp
    src/monitor_view.cpp
    src/sampler.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(${PROJECT_NAME} PRIVATE ftxui::ftxui advapi32 pdh iphlpapi ntdll)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
endif()
