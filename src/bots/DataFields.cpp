#include "DataFields.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace Bots
{
    namespace DataFields
    {
        bool ReadNumber(std::string const& word, std::uint32_t& value)
        {
            if (word.empty() || word.size() > 10)
                return false;
            for (char c : word)
                if (c < '0' || c > '9')
                    return false;
            unsigned long long const number = std::strtoull(word.c_str(), nullptr, 10);
            if (number > 0xFFFFFFFFull)
                return false;
            value = static_cast<std::uint32_t>(number);
            return true;
        }

        bool ReadFloat(std::string const& word, float& value)
        {
            char* end = nullptr;
            errno = 0;
            float const number = std::strtof(word.c_str(), &end);
            if (word.empty() || *end != '\0' || errno != 0 || !std::isfinite(number))
                return false;
            value = number;
            return true;
        }

        bool ReadPosition(std::vector<std::string> const& words, std::size_t first, Position& position)
        {
            return words.size() >= first + 3 && ReadFloat(words[first], position.x) &&
                ReadFloat(words[first + 1], position.y) && ReadFloat(words[first + 2], position.z);
        }

        std::string FormatFloat(float value)
        {
            char text[32];
            std::snprintf(text, sizeof(text), "%.2f", value);
            std::string result = text;
            if (result == "-0.00")
                result = "0.00";
            return result;
        }

        std::string FormatPosition(Position const& position)
        {
            return FormatFloat(position.x) + " " + FormatFloat(position.y) + " " + FormatFloat(position.z);
        }
    }
}
