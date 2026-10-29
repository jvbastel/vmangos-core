#ifndef WOWAI_BOTS_ROLE_CHOICE_H
#define WOWAI_BOTS_ROLE_CHOICE_H

#include <string>

// jvbastel/WOWAI#197: the role choice of a human player, from the role choice
// add-on (wowai/addons/WowaiRole). The bots know no role of a human
// otherwise: a 1.12 party keeps none.
namespace Bots
{
    enum class RoleChoice
    {
        None, // No add-on, no pick, or a message that the bots cannot read.
        Tank,
        Healer,
        Melee,  // Melee damage.
        Ranged, // Ranged damage.
    };

    // The add-on prefix of the role choice messages, and the version of the
    // text. A new text format takes a new version: a bot reads only its own.
    char const* const RoleChoicePrefix = "WOWAI_ROLE";
    char const* const RoleChoiceVersion = "1";

    struct RoleChoiceMessage
    {
        // The message has the prefix of the role choice add-on. False: an
        // add-on message of another add-on, which the bots ignore.
        bool ours;
        // RoleChoice::None for a bad or unknown-version text.
        RoleChoice choice;
    };

    // Reads a whisper of `sender` to `receiver` as the server gets it
    // (spec #188 fix-up S4): the add-on whispers to the player's own name,
    // as the bot panel does (BotPanel.h), so that it works with no group.
    // The text is the prefix, a space, then "<version>:<role>", with role
    // one of tank, healer, melee, ranged or none, in lower case. For example
    // "WOWAI_ROLE 1:tank". Ours only to the sender's own name (in any case).
    RoleChoiceMessage ParseRoleChoiceWhisper(std::string const& sender, std::string const& receiver,
        std::string const& text);
}

#endif
