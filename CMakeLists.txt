cmake_minimum_required(VERSION 3.24)

if(POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
endif()

project(ComputerGr LANGUAGES CXX)

if(MSVC)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
      "$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>")
endif()

add_definitions(-DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)

add_subdirectory(lab1)
