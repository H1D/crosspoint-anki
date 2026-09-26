add_executable(AnkiClientTest
  AnkiClientTest.cpp
  ${REPO_ROOT}/lib/AnkiClient/AnkiJson.cpp
  ${REPO_ROOT}/lib/AnkiClient/AnkiMarkup.cpp
  ${REPO_ROOT}/lib/AnkiClient/AnkiJournal.cpp
  ${REPO_ROOT}/lib/AnkiClient/AnkiNoteQueue.cpp
  ${REPO_ROOT}/lib/AnkiClient/AnkiCardCache.cpp
  ${REPO_ROOT}/lib/AnkiClient/AnkiSyncEngine.cpp
)

target_include_directories(AnkiClientTest PRIVATE
  ${REPO_ROOT}/lib/AnkiClient
)

target_link_libraries(AnkiClientTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(AnkiClientTest)
