cmake_minimum_required(VERSION 3.16)
project(SPZ_Qt VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable Qt features
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Find Qt
# Note: If CMake cannot find Qt, please set CMAKE_PREFIX_PATH in CMake or VS settings to your Qt msvc2019_64 directory
# Example: set(CMAKE_PREFIX_PATH "C:/Qt/6.6.2/msvc2019_64")
set(CMAKE_PREFIX_PATH "C:/Qt/6.11.0/msvc2022_64" CACHE STRING "")
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Charts Sql Network)

# Source files
set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/backend.cpp
    src/backend.h
    src/process_info.h
    src/anomaly_types.h
    src/anomaly_dialogs.cpp
    src/anomaly_dialogs.h
    src/database.cpp
    src/database.h
    src/baseline_tracker.cpp
    src/baseline_tracker.h
    src/anomaly_engine.cpp
    src/anomaly_engine.h
    src/recommend_engine.cpp
    src/recommend_engine.h
    src/process_scanner.cpp
    src/process_scanner.h
    src/alert_manager.cpp
    src/alert_manager.h
    src/settings_manager.cpp
    src/settings_dialog.cpp
)

# Add executable
add_executable(SPZ_Qt WIN32 ${SOURCES})

# Link libraries
target_link_libraries(SPZ_Qt PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Charts Qt6::Sql Qt6::Network)
target_link_libraries(SPZ_Qt PRIVATE pdh.lib psapi.lib iphlpapi.lib ws2_32.lib) # Windows Perf Counters + Network
