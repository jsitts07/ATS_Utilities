#pragma once
#include <vector>
#include <string>
#include <functional>
#include "core.hpp"

namespace ts_extra_utilities::pattern_scanner
{
    struct PatternCandidate
    {
        std::string pattern;
        std::string description;
        int32_t offset = 0;
        std::function<bool(uint64_t)> validator = nullptr;
    };

    class RobustPatternScanner
    {
    public:
        static uint64_t find_with_fallbacks(
            const std::string& name,
            const std::vector<PatternCandidate>& candidates
        );

        static bool validate_base_ctrl_pattern(uint64_t address);
        static bool validate_function_pattern(uint64_t address);
    };

    // Pre-defined pattern sets for common functions
    namespace patterns
    {
        extern const std::vector<PatternCandidate> BASE_CTRL_PATTERNS;
        extern const std::vector<PatternCandidate> SET_INDIVIDUAL_STEERING_PATTERNS;
        extern const std::vector<PatternCandidate> CRASH_FUNCTION_PATTERNS;
        extern const std::vector<PatternCandidate> CONNECT_SLAVE_PATTERNS;
    }
}