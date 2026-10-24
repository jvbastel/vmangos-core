#include "ChatClient.h"

std::unique_ptr<Bots::ChatConnection> Bots::NewHttpChatConnection(ChatClientSettings const&) { return nullptr; }
