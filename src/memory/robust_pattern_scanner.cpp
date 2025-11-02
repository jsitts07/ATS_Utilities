#include "robust_pattern_scanner.hpp"
#include "memory_utils.hpp"
#include <Windows.h>

using namespace ts_extra_utilities;
using namespace ts_extra_utilities::memory;

namespace ts_extra_utilities::pattern_scanner
{
    uint64_t RobustPatternScanner::find_with_fallbacks(
        const std::string& name,
        const std::vector<PatternCandidate>& candidates)
    {
        CCore::g_instance->debug("Scanning for %s with %zu candidates", name.c_str(), candidates.size());

        for (size_t i = 0; i < candidates.size(); ++i)
        {
            const auto& candidate = candidates[i];
            CCore::g_instance->debug("Trying pattern %zu: %s (%s)", i + 1, candidate.description.c_str(), candidate.pattern.c_str());

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
                CCore::g_instance->debug("Pattern %zu failed validation", i + 1);
                continue;
            }

            CCore::g_instance->info("Successfully found %s using pattern %zu: %s at +0x%llx", 
                name.c_str(), i + 1, candidate.description.c_str(), memory::as_offset(address));
            return address;
        }

        CCore::g_instance->error("Failed to find %s - all %zu patterns failed", name.c_str(), candidates.size());
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

    uint64_t RobustPatternScanner::find_function_near_address(
        uint64_t known_address,
        const std::vector<PatternCandidate>& patterns,
        size_t search_range)
    {
        if (known_address == 0) return 0;
        
        CCore::g_instance->info("Searching for function patterns near address +0x%lx within range 0x%lx",
                       known_address - reinterpret_cast<uint64_t>(GetModuleHandle(nullptr)),
                       search_range);
        
        // Search in both directions from the known function
        for (size_t offset = 0x1000; offset < search_range; offset += 0x1000) {
            // Search forward
            if (known_address + offset > known_address) { // Check for overflow
                uint64_t forward_addr = known_address + offset;
                for (size_t i = 0; i < patterns.size(); ++i) {
                    const auto& pattern = patterns[i];
                    
                    // Scan a 4KB region around this address
                    auto result = pattern::scan(
                        pattern.pattern.c_str(),
                        forward_addr,
                        0x1000
                    );
                    
                    if (result != 0) {
                        uint64_t candidate = result + pattern.offset;
                        if (pattern.validator && pattern.validator(candidate)) {
                            CCore::g_instance->info("Found function using pattern %zu: %s at +0x%lx (forward search, offset +0x%lx)",
                                           i + 1, pattern.description.c_str(),
                                           candidate - reinterpret_cast<uint64_t>(GetModuleHandle(nullptr)),
                                           offset);
                            return candidate;
                        }
                    }
                }
            }
            
            // Search backward (avoid underflow)
            if (offset <= known_address && known_address - offset > 0) {
                uint64_t backward_addr = known_address - offset;
                for (size_t i = 0; i < patterns.size(); ++i) {
                    const auto& pattern = patterns[i];
                    
                    // Scan a 4KB region around this address
                    auto result = pattern::scan(
                        pattern.pattern.c_str(),
                        backward_addr,
                        0x1000
                    );
                    
                    if (result != 0) {
                        uint64_t candidate = result + pattern.offset;
                        if (pattern.validator && pattern.validator(candidate)) {
                            CCore::g_instance->info("Found function using pattern %zu: %s at +0x%lx (backward search, offset -0x%lx)",
                                           i + 1, pattern.description.c_str(),
                                           candidate - reinterpret_cast<uint64_t>(GetModuleHandle(nullptr)),
                                           offset);
                            return candidate;
                        }
                    }
                }
            }
        }
        
        CCore::g_instance->error("Proximity search failed - no valid function patterns found near known address");
        return 0;
    }

    uint64_t RobustPatternScanner::analyze_binary_around_function(
        uint64_t known_function_address,
        const std::string& target_function_name)
    {
        if (known_function_address == 0) return 0;
        
        CCore::g_instance->info("Starting aggressive binary analysis around connect_slave function...");
        
        uint64_t base_addr = reinterpret_cast<uint64_t>(GetModuleHandle(nullptr));
        uint64_t relative_addr = known_function_address - base_addr;
        
        CCore::g_instance->info("Analyzing binary around +0x%lx", relative_addr);
        
        std::vector<uint64_t> candidates;
        
        // Strategy 1: Look for function prologues in nearby memory with stricter validation
        for (int offset = -0x100000; offset <= 0x100000; offset += 0x10) {
            if (offset == 0) continue; // Skip the known function itself
            
            uint64_t test_addr = known_function_address + offset;
            
            // Use safer memory access checking
            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery(reinterpret_cast<LPCVOID>(test_addr), &mbi, sizeof(mbi)) == 0) {
                continue;
            }
            
            if (mbi.State != MEM_COMMIT || !(mbi.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) {
                continue;
            }
            
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(test_addr);
            
            // Look for more specific patterns that match crashes_when_disconnected signature
            // This function should take 2 parameters and have crash prevention logic
            
            // Pattern 1: Standard prologue with parameter handling
            if (bytes[0] == 0x40 && bytes[1] == 0x53 && 
                bytes[2] == 0x48 && bytes[3] == 0x83 && bytes[4] == 0xec) {
                
                // Must have null pointer checks AND parameter usage
                bool has_null_check = false;
                bool has_param_access = false;
                bool has_conditional_return = false;
                
                for (int i = 5; i < 60; i++) {
                    // Check for null pointer tests
                    if ((bytes[i] == 0x48 && bytes[i+1] == 0x85) || // test reg, reg
                        (bytes[i] == 0x48 && bytes[i+1] == 0x83 && bytes[i+3] == 0x00)) { // cmp [reg], 0
                        has_null_check = true;
                    }
                    
                    // Check for parameter access (accessing struct members)
                    if (bytes[i] == 0x48 && bytes[i+1] == 0x8b && i+6 < 60) {
                        uint32_t offset_val = *reinterpret_cast<const uint32_t*>(&bytes[i+2]);
                        if (offset_val > 0x50 && offset_val < 0x500) { // Reasonable trailer struct offsets
                            has_param_access = true;
                        }
                    }
                    
                    // Check for conditional returns (early exit on null)
                    if (bytes[i] == 0x74 || bytes[i] == 0x75) { // je/jne short
                        if (i+2 < 60 && (bytes[i+2] == 0xc3 || // ret
                                       (bytes[i+2] == 0x48 && bytes[i+3] == 0x83))) { // or cleanup before ret
                            has_conditional_return = true;
                        }
                    }
                }
                
                if (has_null_check && has_param_access && has_conditional_return) {
                    CCore::g_instance->info("Found strong candidate for crashes_when_disconnected at +0x%lx (all validation criteria met)",
                        test_addr - base_addr);
                    candidates.push_back(test_addr);
                }
            }
        }
        
        // Return the first strong candidate, or 0 if none found
        if (!candidates.empty()) {
            CCore::g_instance->info("Binary analysis found %zu candidate(s), using first one", candidates.size());
            return candidates[0];
        }
        
        CCore::g_instance->error("Binary analysis failed to find validated crashes_when_disconnected function");
        return 0;
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
            },
            {
                "48 85 d2 74 ? 48 89 5c 24 ? 48 89 6c 24 ? 48 89 74 24 ?",
                "SDK 1.14 pattern variant 1 (simplified prologue)",
                0,
                RobustPatternScanner::validate_function_pattern
            },
            {
                "48 85 d2 0f 84 ? ? ? ? 48 89 5c 24 ? 57 48 83 ec ?",
                "SDK 1.14 pattern variant 2 (different register handling)",
                0,
                RobustPatternScanner::validate_function_pattern
            },
            {
                "48 85 d2 74 ? 48 83 ec ? 48 89 5c 24 ? 48 89 74 24 ?",
                "SDK 1.14 pattern variant 3 (compact prologue)",
                0,
                RobustPatternScanner::validate_function_pattern
            },
            {
                "48 89 5c 24 ? 57 48 83 ec ? 48 85 d2 74 ?",
                "SDK 1.14 pattern variant 4 (reordered null check)",
                0,
                RobustPatternScanner::validate_function_pattern
            },
            {
                "48 89 5c 24 ? 48 89 74 24 ? 57 48 83 ec ? 48 85 d2",
                "SDK 1.14 pattern variant 5 (modern prologue + null check)",
                0,
                RobustPatternScanner::validate_function_pattern
            },
            {
                "40 53 48 83 ec ? 48 85 d2 48 8b d9 74 ?",
                "SDK 1.14 pattern variant 6 (minimal prologue)",
                0,
                RobustPatternScanner::validate_function_pattern
            },
            {
                "48 83 ec ? 48 85 d2 74 ? 48 89 5c 24 ?",
                "SDK 1.14 pattern variant 7 (ultra-compact)",
                0,
                RobustPatternScanner::validate_function_pattern
            },
            // NEW: Super specific patterns for crashes_when_disconnected function signature
            {
                "48 85 c9 74 ? 48 85 d2 74 ? 48 83 ec ? 48 89 5c 24 ?",
                "SDK 1.14 crashes_when_disconnected v1 (dual null check)",
                0,
                RobustPatternScanner::validate_function_pattern
            },
            {
                "48 85 c9 0f 84 ? ? ? ? 48 85 d2 0f 84 ? ? ? ?",
                "SDK 1.14 crashes_when_disconnected v2 (long jumps)",
                0,
                RobustPatternScanner::validate_function_pattern
            },
            {
                "40 53 48 83 ec ? 48 85 c9 74 ? 48 85 d2 74 ?",
                "SDK 1.14 crashes_when_disconnected v3 (standard prologue)",
                0,
                RobustPatternScanner::validate_function_pattern
            },
            {
                "48 89 5c 24 ? 48 83 ec ? 48 85 c9 74 ? 48 85 d2 74 ?",
                "SDK 1.14 crashes_when_disconnected v4 (save + dual check)",
                0,
                RobustPatternScanner::validate_function_pattern
            },
            {
                "48 83 ec ? 48 85 c9 0f 84 ? ? ? ? 48 85 d2 0f 84",
                "SDK 1.14 crashes_when_disconnected v5 (compact dual check)",
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
            },
            {
                "48 89 5c 24 ? 57 48 83 ec ? 48 83 b9 ? ? ? ? ? 48 8b d9",
                "SDK 1.14 pattern variant 1 (modern prologue)",
                0,
                RobustPatternScanner::validate_function_pattern
            },
            {
                "40 53 48 83 ec ? 48 83 b9 ? ? ? ? ? 48 8b d9 74 ?",
                "SDK 1.14 pattern variant 2 (simplified check)",
                0,
                RobustPatternScanner::validate_function_pattern
            },
            {
                "48 83 ec ? 48 89 5c 24 ? 48 83 b9 ? ? ? ? ? 48 8b d9",
                "SDK 1.14 pattern variant 3 (compact form)",
                0,
                RobustPatternScanner::validate_function_pattern
            },
            {
                "48 89 5c 24 ? 48 83 ec ? 48 8b d9 48 83 b9 ? ? ? ? ?",
                "SDK 1.14 pattern variant 4 (reordered operations)",
                0,
                RobustPatternScanner::validate_function_pattern
            }
        };
    }
}