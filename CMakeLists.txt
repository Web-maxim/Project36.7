cmake_minimum_required(VERSION 3.21)
project(AdminServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

qt_add_executable(AdminServer WIN32
    main.cpp
    MainWindow.h
    MainWindow.cpp
)

target_link_libraries(AdminServer PRIVATE Qt6::Widgets)
