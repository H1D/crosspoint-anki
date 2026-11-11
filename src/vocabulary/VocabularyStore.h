#pragma once

#include <cstdint>
#include <optional>
#include <string>

struct VocabularyContext {
  std::string title;
  std::string author;
  std::string series;
  std::optional<float> seriesIndex;
  std::string chapter;
  uint16_t page = 0;
  uint16_t spine = 0;
  uint32_t startOffset = UINT32_MAX;
  // Fork: the Anki note's sentence and Back field, when the lookup could have
  // been added to Anki, so a batch send makes the same card as Add to Anki.
  std::string sentence;
  std::string ankiBack;
};

class VocabularyStore {
 public:
  static constexpr const char* FILE_PATH = "/Vocabulary.txt";

  // A failed export must never prevent a successful dictionary lookup.
  static bool append(const VocabularyContext& context, const std::string& word, const std::string& headword,
                     const std::string& definition, bool htmlDefinition);
};
