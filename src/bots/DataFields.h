#ifndef WOWAI_BOTS_DATA_FIELDS_H
#define WOWAI_BOTS_DATA_FIELDS_H

#include "BotTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The one reader and writer of the number words of the route files and the
// map export (Route.h, RouteExport.h): whole numbers, floats with two
// decimals, and positions as three floats.
namespace Bots
{
    namespace DataFields
    {
        // A whole number of 0 to 4294967295, only digits.
        bool ReadNumber(std::string const& word, std::uint32_t& value);
        // A finite float, the whole word.
        bool ReadFloat(std::string const& word, float& value);
        // Three floats from words[first]; false when there are fewer words.
        bool ReadPosition(std::vector<std::string> const& words, std::size_t first, Position& position);

        // Two decimals; -0.00 is written as 0.00.
        std::string FormatFloat(float value);
        // "x y z", each as FormatFloat.
        std::string FormatPosition(Position const& position);
    }
}

#endif
