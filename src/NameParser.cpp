#include "NameParser.hpp"
#include <vector>
#include <stdexcept>

NameParser::NameParser(std::string src)
{
    // Check for leading slash or multiple slashes.
    if(src.empty() || src.front() == '/' || src.find("//") != std::string::npos) 
    {
        throw std::invalid_argument("Input string cannot start with a slash or contain multiple consecutive slashes.");
    }

    size_t slash_pos = src.find("/");
    if(slash_pos == std::string::npos) 
    {
        // No slash found, entire string is considered the library name.
        this->org = ""; // Set organization to an empty string
        this->library = src;
    }
    else if(slash_pos == src.length() - 1) 
    {
        // Slash is at the end of the string, indicating no library name is provided.
        throw std::invalid_argument("Input string cannot end with a slash.");
    }
    else 
    {
        // Split the string into organization and library.
        this->org = src.substr(0, slash_pos);
        this->library = src.substr(slash_pos + 1);

        // Check if there is another slash after the first one.
        if (this->library.find("/") != std::string::npos) {
            throw std::invalid_argument("Input string cannot contain multiple slashes.");
        }
    }
}
