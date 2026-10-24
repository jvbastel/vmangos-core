#ifndef WOWAI_BOTS_DATA_TOKENIZER_H
#define WOWAI_BOTS_DATA_TOKENIZER_H

#include <cstddef>
#include <string>
#include <vector>

namespace Bots
{
    // One line of a data file with words on it.
    struct DataLine
    {
        std::size_t number; // 1 for the first line of the file.
        std::vector<std::string> words;
    };

    std::vector<DataLine> TokenizeLines(std::string const& text);
}

#endif
