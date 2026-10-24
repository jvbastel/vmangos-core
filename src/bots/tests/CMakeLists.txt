# GoogleTest tests of the bot decision library. They need no running core.
# Install GoogleTest with the `libgtest-dev` package.
find_package(GTest REQUIRED)

add_executable(bots_tests
  BotAccountsTest.cpp
  BotCommandTest.cpp
  BotRecipeTest.cpp
  CommandStateTest.cpp
  DataTokenizerTest.cpp
  FakeBotTest.cpp
  GearDrawTest.cpp
  GearTemplateTest.cpp
  GroupCoordinatorTest.cpp
  InboxTest.cpp
  RouteTest.cpp
  SpellBookTest.cpp
  SpellDataTest.cpp
  SpellFileTest.cpp
  StrategyTest.cpp
  ThinkTimerTest.cpp
)
# GoogleTest 1.17 needs C++17. The library itself stays C++14, like the core.
set_target_properties(bots_tests PROPERTIES CXX_STANDARD 17)
target_link_libraries(bots_tests PRIVATE bots GTest::gtest GTest::gtest_main)

# The data files of the bots (bots/data in the repository), and the test data
# of this folder. wowai/bots-test.sh sets BOT_DATA_DIR, because it builds a
# copy of src/bots alone.
set(BOT_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../bots/data" CACHE PATH "Folder of the bot data files")
target_compile_definitions(bots_tests PRIVATE
  BOT_DATA_DIR="${BOT_DATA_DIR}"
  BOT_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)

include(GoogleTest)
gtest_discover_tests(bots_tests)
