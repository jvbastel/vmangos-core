# The decision library of the bots. See ADR 0002 of WOWAI: it includes no
# game header and sees the game only through BotView and BotActions.
#
# This file works in two ways:
# - In the core build, as `add_subdirectory(bots)` from src/CMakeLists.txt.
# - Alone, as its own project: `cmake -S src/bots -B <dir>`. `wowai/bots-test.sh`
#   builds a copy of this folder alone, so that a game header cannot be found.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.14)
  project(WowaiBots CXX)
  set(CMAKE_CXX_STANDARD 14)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  option(BUILD_BOT_TESTS "Build the GoogleTest tests of the bot decision library" ON)
  if(BUILD_BOT_TESTS)
    enable_testing()
  endif()
endif()

add_library(bots STATIC
  BotAccounts.cpp
  BotRecipe.cpp
  DataTokenizer.cpp
  GearTemplate.cpp
  ThinkTimer.cpp
)
target_include_directories(bots PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(BUILD_BOT_TESTS)
  add_subdirectory(tests)
endif()
