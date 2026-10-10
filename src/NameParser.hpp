#pragma once

#include <string>

// Splits a plugin or pak name into its organization and library parts and
// rejects every name that could address anything but a plain file below a
// search location. Throws LoadError (InvalidName) before any path is built.
// kind names what is being loaded ("component plugin", "system plugin",
// "resource pak") and appears in the message.
class NameParser
{
  public:
    NameParser(const std::string &name, const std::string &kind);
    std::string org;
    std::string library;
};
