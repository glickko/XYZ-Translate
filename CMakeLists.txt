cmake_minimum_required(VERSION 3.16)
project(XYZ-Translate VERSION 4.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#automation
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

#findmsys
find_package(Qt6 COMPONENTS Core Gui Widgets Network Sql REQUIRED)

# no terminal win32
add_executable(XYZ-Translate WIN32
    src/main.cpp
    resources.rc # Hapus baris ini kalau lu nggak pakai file resources.rc untuk ikon EXE
)

# win32 dwm api and qt sync xyz uwu
target_link_libraries(XYZ-Translate PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Sql
    dwmapi
    user32
)
