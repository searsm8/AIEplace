#pragma once

#include <string>

/**
 * @brief Utilities for handling JSON with comments
 *
 * Allows // and # style comments in JSON files for better readability.
 * This utility strips comments so the content can be parsed by a standard JSON parser.
 */
namespace JsonUtils {

    /**
     * @brief Strip single-line comments from JSON content
     *
     * Removes // and # style comments while preserving strings.
     * This allows JSON parsers to process files that contain comments.
     *
     * @param content The JSON file content (with comments)
     * @return JSON content with comments removed
     */
    std::string stripComments(const std::string& content);

}

