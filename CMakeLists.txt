CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(chat_admin_gui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)   # Важно для Q_OBJECT

# Путь к Qt, если нужно:
# set(CMAKE_PREFIX_PATH "C:/Qt/6.6.3/msvc2019_64")

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(chat_admin_gui
    main.cpp
    MainWindow.cpp
    MainWindow.h
    server.cpp
    server.h
    client.cpp
    net_proto.h
    Database.cpp
    Database.h
    Config.cpp    
    sha1.cpp
    sha1.h
    sqlite3.c
    sqlite3.h
)

target_link_libraries(chat_admin_gui PRIVATE Qt6::Widgets)

# Windows: консоль нужна для сервера/лога
if (WIN32)
  target_link_libraries(chat_admin_gui PRIVATE ws2_32) # на всякий случай, несмотря на #pragma
  target_compile_definitions(chat_admin_gui PRIVATE UNICODE _UNICODE)
endif()
