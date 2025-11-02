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

        // Proximity-based search for functions near a known address
        static uint64_t find_function_near_address(
            uint64_t known_address,
            const std::vector<PatternCandidate>& patterns,
            size_t search_range = 0x100000  // 1MB range
        );

        // Aggressive binary analysis to find related functions
        static uint64_t analyze_binary_around_function(
            uint64_t known_function_address,
            const std::string& target_function_name
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