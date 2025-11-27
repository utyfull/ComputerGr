cmake_minimum_required(VERSION 3.24)

# Политика можно оставить, но формат выставляем отдельно
if(POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
endif()

project(ComputerGr LANGUAGES CXX)

# Только для MSVC и только для Debug/RelWithDebInfo -> PDB
if(MSVC)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
      "$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>")
endif()

add_definitions(-DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN)

add_subdirectory(lab1)
