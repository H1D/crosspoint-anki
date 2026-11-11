add_executable(AnkiClientTest
  AnkiClientTest.cpp
  AnkiNoteFrontTest.cpp
  AnkiPushChunksTest.cpp
  AnkiTypeAnswerTest.cpp
  AnkiStatsTest.cpp
  AnkiVocabularyTest.cpp
  ${REPO_ROOT}/lib/AnkiClient/AnkiJson.cpp
  ${REPO_ROOT}/lib/AnkiClient/AnkiMarkup.cpp
  ${REPO_ROOT}/lib/AnkiClient/AnkiJournal.cpp
  ${REPO_ROOT}/lib/AnkiClient/AnkiNoteQueue.cpp
  ${REPO_ROOT}/lib/AnkiClient/AnkiCardCache.cpp
  ${REPO_ROOT}/lib/AnkiClient/AnkiSyncEngine.cpp
  ${REPO_ROOT}/lib/AnkiClient/AnkiConnectClient.cpp
  ${REPO_ROOT}/lib/AnkiClient/AnkiHtml.cpp
  ${REPO_ROOT}/lib/AnkiClient/AnkiTypeAnswer.cpp
  ${REPO_ROOT}/lib/AnkiClient/AnkiStats.cpp
  ${REPO_ROOT}/lib/AnkiClient/AnkiVocabulary.cpp
)

target_include_directories(AnkiClientTest PRIVATE
  ${REPO_ROOT}/lib/AnkiClient
)

target_link_libraries(AnkiClientTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(AnkiClientTest)
