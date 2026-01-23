cmake_minimum_required(VERSION 3.20)
project(nexus-ledger VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)

set(SOURCES
    src/main.cpp
    # ... add other source files here ...
)

add_executable(${PROJECT_NAME} ${SOURCES})
