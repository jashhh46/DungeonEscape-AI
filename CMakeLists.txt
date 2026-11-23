cmake_minimum_required(VERSION 3.28)
project(DungeonEscape LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(FetchContent)
FetchContent_Declare(SFML
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG 3.1.0
    GIT_SHALLOW ON
    EXCLUDE_FROM_ALL
    SYSTEM)
FetchContent_MakeAvailable(SFML)

add_executable(DungeonEscape src/main.cpp)
target_compile_features(DungeonEscape PRIVATE cxx_std_17)
target_link_libraries(DungeonEscape PRIVATE SFML::Graphics)

add_custom_command(TARGET DungeonEscape POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/Assets"
        "$<TARGET_FILE_DIR:DungeonEscape>/assets"
    COMMENT "Copying game assets next to DungeonEscape"
)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT DungeonEscape)

include(CTest)
if(BUILD_TESTING)
    add_executable(BehaviourTreeTests tests/BehaviourTreeTests.cpp)
    target_compile_features(BehaviourTreeTests PRIVATE cxx_std_17)
    target_include_directories(BehaviourTreeTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(BehaviourTreeTests PRIVATE SFML::System)
    add_test(NAME BehaviourTreeTests COMMAND BehaviourTreeTests)
endif()
