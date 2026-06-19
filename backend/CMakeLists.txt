cmake_minimum_required(VERSION 3.20)
project(medicament_backend C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if (MSVC)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

add_executable(medicament_backend
  src/main.c
  src/app.c
)

target_include_directories(medicament_backend PRIVATE include)

find_package(CURL REQUIRED)
find_package(cJSON CONFIG REQUIRED)

target_link_libraries(medicament_backend PRIVATE CURL::libcurl cJSON::cJSON)

if (WIN32)
  target_link_libraries(medicament_backend PRIVATE ws2_32)
endif()
