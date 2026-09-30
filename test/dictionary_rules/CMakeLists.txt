add_executable(DictionaryRulesTest
  DictionaryRulesTest.cpp
  WikDictTest.cpp
  ${REPO_ROOT}/src/util/DictLanguage.cpp
  ${REPO_ROOT}/src/util/DictStemmer.cpp
  ${REPO_ROOT}/src/util/WikDict.cpp
  ${REPO_ROOT}/lib/AnkiClient/AnkiHtml.cpp
  ${REPO_ROOT}/lib/AnkiClient/AnkiMarkup.cpp
)

target_include_directories(DictionaryRulesTest PRIVATE
  ${REPO_ROOT}/src/util
  ${REPO_ROOT}/lib/AnkiClient
)

target_link_libraries(DictionaryRulesTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(DictionaryRulesTest)
