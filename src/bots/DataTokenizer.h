#ifndef WOWAI_BOTS_DATA_TOKENIZER_H
#define WOWAI_BOTS_DATA_TOKENIZER_H

#include <cstddef>
#include <string>
#include <vector>

// The one tokenizer for every line-based data file of the bots: script
// files, route files, gear templates, talent builds, spell files, and the ban
// file. All of them share its rules:
// - Words are split by spaces and tabs.
// - The comment marker starts a comment that runs to the end of the line,
//   also in the middle of a line or a word.
// - Lines with no words are dropped. Each kept line has its line number, for
//   error messages of the parsers.
namespace Bots
{
    // The comment marker that all data files share.
    constexpr char CommentMarker = '#';

    // One line of a data file with words on it.
    struct DataLine
    {
        std::size_t number; // 1 for the first line of the file.
        std::vector<std::string> words;
    };

    std::vector<DataLine> TokenizeLines(std::string const& text);
}

#endif
