# GoogleTest tests of the bot decision library. They need no running core.
# Install GoogleTest with the `libgtest-dev` package.
find_package(GTest REQUIRED)

add_executable(bots_tests
  BotAccountsTest.cpp
  BotRecipeTest.cpp
  DataTokenizerTest.cpp
  FakeBotTest.cpp
  ThinkTimerTest.cpp
)
# GoogleTest 1.17 needs C++17. The library itself stays C++14, like the core.
set_target_properties(bots_tests PROPERTIES CXX_STANDARD 17)
target_link_libraries(bots_tests PRIVATE bots GTest::gtest GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(bots_tests)
