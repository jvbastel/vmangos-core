#include "DataTokenizer.h"

#include <sstream>

namespace Bots
{
    std::vector<DataLine> TokenizeLines(std::string const& text)
    {
        std::vector<DataLine> lines;
        std::istringstream input(text);
        std::string line;
        std::size_t number = 0;
        while (std::getline(input, line))
        {
            ++number;
            std::istringstream lineInput(line);
            DataLine dataLine{number, {}};
            std::string word;
            while (lineInput >> word)
                dataLine.words.push_back(word);
            lines.push_back(dataLine);
        }
        return lines;
    }
}
