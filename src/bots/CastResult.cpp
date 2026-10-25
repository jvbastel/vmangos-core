#include "CastResult.h"

namespace Bots
{
    namespace
    {
#define WOWAI_CAST_RESULT_NAME(name) #name,
        char const* const FailedNames[] = { WOWAI_SPELL_FAILED_RESULTS(WOWAI_CAST_RESULT_NAME) };
#undef WOWAI_CAST_RESULT_NAME

        char const* CastResultName(std::uint32_t result)
        {
            if (result == SpellCastOk)
                return "SPELL_CAST_OK";
            if (result < sizeof(FailedNames) / sizeof(FailedNames[0]))
                return FailedNames[result];
            return "unknown";
        }
    }

    std::string CastResultText(std::uint32_t result)
    {
        return "result " + std::to_string(result) + " (" + CastResultName(result) + ")";
    }
}
