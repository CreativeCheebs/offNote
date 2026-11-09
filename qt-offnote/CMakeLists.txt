cmake_minimum_required(VERSION 3.21)
project(OffnoteQt LANGUAGES CXX RC)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTORCC ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(yaml-cpp REQUIRED)

# Config, routing, SQLite outbox and delivery: no widgets, shared by the app and the tests.
add_library(offnote_core STATIC
    config.cpp router.cpp shortcut.cpp store.cpp delivery.cpp)
target_link_libraries(offnote_core PUBLIC Qt6::Core yaml-cpp::yaml-cpp sqlite3 winhttp)

add_executable(OffnoteQt WIN32 main.cpp settingswindow.cpp resources.qrc app.rc)
target_link_libraries(OffnoteQt PRIVATE offnote_core Qt6::Widgets)

add_executable(OffnoteQtTests tests/core_tests.cpp)
target_link_libraries(OffnoteQtTests PRIVATE offnote_core)
enable_testing()
add_test(NAME core COMMAND OffnoteQtTests)
