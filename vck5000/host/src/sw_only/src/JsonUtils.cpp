/**
 * @file JsonUtils.cpp
 * @brief Implementation of the comment-stripping JSON preprocessor declared in JsonUtils.h.
 */
#include "JsonUtils.h"

namespace JsonUtils {

std::string stripComments(const std::string& content) {
    std::string result;
    result.reserve(content.size());

    bool in_string = false;
    bool escape_next = false;

    for (size_t i = 0; i < content.size(); ++i) {
        char c = content[i];

        // Handle escape sequences
        if (escape_next) {
            result += c;
            escape_next = false;
            continue;
        }

        if (c == '\\' && in_string) {
            escape_next = true;
            result += c;
            continue;
        }

        // Toggle string state on quotes
        if (c == '"' && !escape_next) {
            in_string = !in_string;
            result += c;
            continue;
        }

        // If we're in a string, just copy the character
        if (in_string) {
            result += c;
            continue;
        }

        // Check for single-line comments outside of strings
        if (c == '/' && i + 1 < content.size() && content[i + 1] == '/') {
            // Skip until end of line
            while (i < content.size() && content[i] != '\n') {
                ++i;
            }
            if (i < content.size()) {
                result += '\n'; // Preserve the newline
            }
            continue;
        }

        if (c == '#') {
            // Skip until end of line
            while (i < content.size() && content[i] != '\n') {
                ++i;
            }
            if (i < content.size()) {
                result += '\n'; // Preserve the newline
            }
            continue;
        }

        // Regular character
        result += c;
    }

    return result;
}

} // namespace JsonUtils
