#include "robust_pattern_scanner.hpp"
#include "memory_utils.hpp"
#include <Windows.h>

namespace ts_extra_utilities::pattern_scanner
{
    uint64_t RobustPatternScanner::find_with_fallbacks(
        const std::string& name,
        const std::vector<PatternCandidate>& candidates)
    {
        CCore::g_instance->debug("Scanning for {} with {} candidates", name, candidates.size());

        for (size_t i = 0; i < candidates.size(); ++i)
        {
            const auto& candidate = candidates[i];
            CCore::g_instance->debug("Trying pattern {}: {} ({})", i + 1, candidate.description, candidate.pattern);

            const auto address = memory::get_address_for_pattern(candidate.pattern.c_str(), candidate.offset);
            
            if (address == 0)
            {
                CCore::g_instance->debug("Pattern {} failed - no match found", i + 1);
                continue;
            }

            CCore::g_instance->debug("Pattern {} found potential match at +{:x}", i + 1, memory::as_offset(address));

            // Run validator if provided
            if (candidate.validator && !candidate.validator(address))
            {
                CCore::g_instance->debug("Pattern {} failed validation", i + 1);
                continue;
            }

            CCore::g_instance->info("Successfully found {} using pattern {}: {} at +{:x}", 
                name, i + 1, candidate.description, memory::as_offset(address));
            return address;
        }

        CCore::g_instance->error("Failed to find {} - all {} patterns failed", name, candidates.size());
        return 0;
    }

    bool RobustPatternScanner::validate_base_ctrl_pattern(uint64_t address)
    {
        // Validate that the address points to valid memory and looks like a pointer reference
        __try
        {
            const auto ptr_address = address + *reinterpret_cast<int32_t*>(address + 3) + 7;
            const auto base_ctrl_ptr = *reinterpret_cast<uint64_t*>(ptr_address);
            
            // Basic validation - should be a valid address in memory
            if (base_ctrl_ptr < 0x10000 || base_ctrl_ptr > 0x7FFFFFFFFFFF)
                return false;

            // Check if it's readable memory
            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery(reinterpret_cast<LPCVOID>(base_ctrl_ptr), &mbi, sizeof(mbi)) == 0)
                return false;

            return (mbi.State == MEM_COMMIT) && (mbi.Protect & PAGE_READONLY || mbi.Protect & PAGE_READWRITE);
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool RobustPatternScanner::validate_function_pattern(uint64_t address)
    {
        // Basic validation for function addresses
        __try
        {
            // Check if the first few bytes look like function prologue
            const auto* bytes = reinterpret_cast<const uint8_t*>(address);
            
            // Common function prologues: push rbp (55), mov rbp, rsp (48 89 e5) or sub rsp, XX (48 83 ec)
            if (bytes[0] == 0x55 || 
                (bytes[0] == 0x48 && (bytes[1] == 0x89 || bytes[1] == 0x83)))
                return true;

            return false;
        }
        __except(EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    namespace patterns
    {
        const std::vector<PatternCandidate> BASE_CTRL_PATTERNS = {
            {
                "48 8b 05 ? ? ? ? 48 8b 4b ? 48 8b 80 ? ? ? ? 48 8b b9",
                "Original pattern (pre-1.14)",
                0,
                RobustPatternScanner::validate_base_ctrl_pattern
            },
            {
                "48 8b 05 ? ? ? ? 48 8b 4f ? 48 8b 80 ? ? ? ? 48 8b b8",
                "Pattern variant 1 (potential 1.14)",
                0,
                RobustPatternScanner::validate_base_ctrl_pattern
            },
            {
                "48 8b 0d ? ? ? ? 48 8b 4b ? 48 8b 81 ? ? ? ? 48 8b b8",
                "Pattern variant 2 (MOV RCX instead of RAX)",
                0,
                RobustPatternScanner::validate_base_ctrl_pattern
            },
            {
                "48 8b ? ? ? ? ? 48 8b ? ? 48 8b 80 ? ? ? ? 48 8b",
                "Relaxed pattern (more wildcards)",
                0,
                RobustPatternScanner::validate_base_ctrl_pattern
            }
        };

        const std::vector<PatternCandidate> SET_INDIVIDUAL_STEERING_PATTERNS = {
            {
                "48 89 5c 24 08 48 89 74 24 10 57 48 83 ec ? 8b 41 ? 48 8b d9 0f 29 74",
                "Original pattern (pre-1.14)",
                0,
                RobustPatternScanner::validate_function_pattern
            },
            {
                "48 89 5c 24 08 48 89 74 24 10 57 48 83 ec ? 8b 41 ? 48 8b da 0f 29 74",
                "Pattern variant 1 (RBX->RDX change)",
                0,
                RobustPatternScanner::validate_function_pattern
            },
            {
                "48 89 5c 24 08 48 89 74 24 10 48 89 7c 24 18 41 56 48 83 ec ? 8b 41",
                "Pattern variant 2 (additional register save)",
                0,
                RobustPatternScanner::validate_function_pattern
            }
        };

        const std::vector<PatternCandidate> CRASH_FUNCTION_PATTERNS = {
            {
                "48 85 d2 0f 84 ? ? ? ? 48 89 74 24 18 57 48 83 ec 40",
                "Original pattern (pre-1.14)",
                0,
                RobustPatternScanner::validate_function_pattern
            },
            {
                "48 85 d2 0f 84 ? ? ? ? 48 89 74 24 10 57 48 83 ec 30",
                "Pattern variant 1 (different stack allocation)",
                0,
                RobustPatternScanner::validate_function_pattern
            },
            {
                "48 85 d2 0f 84 ? ? ? ? 48 89 6c 24 18 48 89 74 24 20",
                "Pattern variant 2 (different register saves)",
                0,
                RobustPatternScanner::validate_function_pattern
            }
        };

        const std::vector<PatternCandidate> CONNECT_SLAVE_PATTERNS = {
            {
                "40 53 48 83 ec 60 48 83 b9 ? ? ? ? 00 48 8b d9 0f 84 ? ? ? ? 48 8d 54 24 ? e8",
                "Original pattern (pre-1.14)",
                0,
                RobustPatternScanner::validate_function_pattern
            },
            {
                "40 53 48 83 ec 50 48 83 b9 ? ? ? ? 00 48 8b d9 0f 84 ? ? ? ? 48 8d 54 24 ? e8",
                "Pattern variant 1 (different stack allocation)",
                0,
                RobustPatternScanner::validate_function_pattern
            },
            {
                "48 89 5c 24 08 48 83 ec 60 48 83 b9 ? ? ? ? 00 48 8b d9 0f 84 ? ? ? ?",
                "Pattern variant 2 (different prologue)",
                0,
                RobustPatternScanner::validate_function_pattern
            }
        };
    }
}