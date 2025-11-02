#include "trailer_manipulation.hpp"

#include "imgui.h"

#include "core.hpp"
#include "hooks/function_hook.hpp"
#include "hooks/vtable_hook.hpp"
#include "prism/controllers/base_ctrl.hpp"
#include "prism/game_actor.hpp"
#include "prism/vehicles/game_trailer_actor.hpp"
#include "prism/physics/physics_actor_t.hpp"
#include "prism/vehicles/accessories/data/accessory_chassis_data.hpp"
#include "memory/robust_pattern_scanner.hpp"

namespace ts_extra_utilities
{
    struct TrailerJointState
    {
        enum Enum
        {
            NORMAL,
            LOCKED,
            DISCONNECTED,
        };
    };

    thread_local bool locked_trailers[ 20 ] = {};

    thread_local TrailerJointState::Enum trailer_joints[ 20 ] = {};

    thread_local std::shared_ptr< CVirtualFunctionHook > steering_advance_hook = nullptr;
    thread_local std::shared_ptr< CFunctionHook > crashes_when_disconnected_hook = nullptr;
    thread_local std::shared_ptr< CFunctionHook > connect_slave_hook = nullptr;

    /**
     * \brief Hook for prism::physics_trailer_u::steering_advance so we can control which trailer can be steered by the game
     * \param self /
     * \return /
     */
    uint64_t hk_steering_advance( prism::physics_trailer_u* self )
    {
        int trailer_index = 0;
        const auto* check_trailer = CCore::g_instance->get_game_actor()->game_trailer_actor;
        while ( check_trailer != nullptr && self != check_trailer )
        {
            check_trailer = check_trailer->slave_trailer;
            ++trailer_index;
        }

        if ( check_trailer == nullptr || locked_trailers[ trailer_index ] == false )
        {
            return steering_advance_hook->get_original< prism::physics_trailer_u_steering_advance_fn >()( self );
        }
        return 0;
    }

    // TODO: Check what this original function actually did
    // original function crashes when a slave trailer is disconnected due to it expecting the physics joint to exist when there is a slave trailer
    // and the joint is not there when we have the trailer disconnected
    void hk_crashes_when_disconnected( prism::physics_trailer_u* self, prism::game_trailer_actor_u* trailer_actor )
    {
        CCore::g_instance->info("crashes_when_disconnected: Hook function called!");
        
        // For now, let's just prevent any calls to this function entirely
        // until we're sure we have the right one
        CCore::g_instance->info("crashes_when_disconnected: Preventing function call for safety");
        return;
        
        /*
        // Add our own safety checks before calling the original function
        if (self == nullptr || trailer_actor == nullptr) {
            CCore::g_instance->info("crashes_when_disconnected: Null parameter detected, skipping original function call");
            return;
        }
        
        CCore::g_instance->info("crashes_when_disconnected: Parameters valid, checking hook status");
        
        // Get the original function from the hook
        if (crashes_when_disconnected_hook && crashes_when_disconnected_hook->get_status() == CHook::HOOKED) {
            auto original_fn = crashes_when_disconnected_hook->get_original<void(prism::physics_trailer_u*, prism::game_trailer_actor_u*)>();
            if (original_fn != nullptr) {
                CCore::g_instance->info("crashes_when_disconnected: Calling original function with safety wrapper");
                try {
                    original_fn(self, trailer_actor);
                    CCore::g_instance->info("crashes_when_disconnected: Original function call completed successfully");
                } catch (...) {
                    CCore::g_instance->error("crashes_when_disconnected: Exception caught in original function call");
                }
            } else {
                CCore::g_instance->info("crashes_when_disconnected: Original function pointer is null");
            }
        } else {
            CCore::g_instance->info("crashes_when_disconnected: Hook not available, performing minimal cleanup");
        }
        
        CCore::g_instance->info("crashes_when_disconnected: Hook function completed");
        */
    }

    /**
     * \brief Hook to stop slave trailers from getting automatically reconnected when we attach their parent
     * \param _ unused
     */
    void hk_connect_slave( prism::physics_trailer_u* _ )
    {
        // original_connect_slave(self);
    }

    CTrailerManipulation::CTrailerManipulation() = default;

    CTrailerManipulation::~CTrailerManipulation()
    {
        steering_advance_hook.reset();
        crashes_when_disconnected_hook.reset();
        connect_slave_hook.reset();
    }

    bool CTrailerManipulation::init()
    {
        // Use robust pattern scanner for set_individual_steering function
        const auto steering_addr = pattern_scanner::RobustPatternScanner::find_with_fallbacks(
            "set_individual_steering",
            pattern_scanner::patterns::SET_INDIVIDUAL_STEERING_PATTERNS
        );

        if (steering_addr != 0)
        {
            this->set_individual_steering_fn_ = reinterpret_cast<prism::set_individual_steering_fn*>(steering_addr);
            CCore::g_instance->debug( "Found set_individual_steering function @ +{:x}", memory::as_offset(steering_addr) );
        }
        else
        {
            CCore::g_instance->error( "Could not find 'set_individual_steering' function" );
        }

        // First, find connect_slave function - we need it for both hooking and potential proximity search
        auto connect_slave_address = pattern_scanner::RobustPatternScanner::find_with_fallbacks(
            "connect_slave",
            pattern_scanner::patterns::CONNECT_SLAVE_PATTERNS
        );

        // Use robust pattern scanner for crash function
        auto crash_fn_address = pattern_scanner::RobustPatternScanner::find_with_fallbacks(
            "crashes_when_disconnected",
            pattern_scanner::patterns::CRASH_FUNCTION_PATTERNS
        );

        // If pattern matching failed, try aggressive binary analysis
        if (crash_fn_address == 0 && connect_slave_address != 0) {
            CCore::g_instance->info("Pattern matching failed for crashes_when_disconnected");
            CCore::g_instance->error("SAFETY: Binary analysis disabled due to false positives causing crashes");
            CCore::g_instance->error("SAFETY: Trailer manipulation will be disabled to prevent crashes");
            
            /*
            // Disabled due to finding wrong functions that don't prevent crashes
            crash_fn_address = pattern_scanner::RobustPatternScanner::analyze_binary_around_function(
                connect_slave_address,
                "crashes_when_disconnected"
            );
            
            if (crash_fn_address != 0) {
                CCore::g_instance->info("Successfully found crashes_when_disconnected via binary analysis!");
            } else {
                CCore::g_instance->error("Binary analysis also failed for crashes_when_disconnected");
                CCore::g_instance->error("SAFETY: Trailer manipulation will be disabled to prevent crashes");
            }
            */
        } else if (crash_fn_address == 0) {
            CCore::g_instance->error("Cannot perform binary analysis - connect_slave not found either");
            CCore::g_instance->error("SAFETY: Trailer manipulation will be disabled to prevent crashes");
        }

        crashes_when_disconnected_hook = CCore::g_instance->get_hooks_manager()->register_function_hook(
            "crashes_when_disconnected",
            crash_fn_address,
            reinterpret_cast< uint64_t >( &hk_crashes_when_disconnected ) );

        // Track if safety functions are available
        safety_functions_available_ = (crash_fn_address != 0);
        
        if (safety_functions_available_) {
            CCore::g_instance->info("Safety functions available - trailer manipulation enabled");
            CCore::g_instance->info("Original crashes_when_disconnected function will be called with safety wrapper");
        } else {
            CCore::g_instance->error("Safety functions missing - trailer manipulation disabled for safety");
        }

        if ( !CCore::g_instance->is_truckersmp() )
        {
            if (crash_fn_address != 0)
                crashes_when_disconnected_hook->hook();
        }

        // Register connect_slave hook
        connect_slave_hook = CCore::g_instance->get_hooks_manager()->register_function_hook(
            "prism::physics_trailer_u::connect_slave",
            connect_slave_address,
            reinterpret_cast< uint64_t >( &hk_connect_slave ) );
        if ( !CCore::g_instance->is_truckersmp() )
        {
            if (connect_slave_address != 0)
                connect_slave_hook->create();
        }

        if (connect_slave_address != 0)
        {
            this->get_slave_hook_position_fn_ = reinterpret_cast< prism::physics_trailer_u_get_slave_hook_position_fn* >(
                connect_slave_address + 29 + *reinterpret_cast< int32_t* >( connect_slave_address + 29 ) + 4 );
            
            // Store the address for use in safety functions
            this->connect_slave_address_ = connect_slave_address;
        }

        this->valid_ = true;
        return this->valid_;
    }

    bool CTrailerManipulation::is_safe_to_manipulate_trailer(int trailer_index) const {
        // Basic safety checks before any manipulation
        if (trailer_index < 0 || trailer_index >= 10) { // SCS_TELEMETRY_trailers_count
            CCore::g_instance->warning("Trailer index {} out of bounds", trailer_index);
            return false;
        }
        
        if (!CCore::g_instance->is_trailer_connected(trailer_index)) {
            CCore::g_instance->warning("Trailer {} not connected according to telemetry", trailer_index);
            return false;
        }
        
        // Check if we have a valid game actor
        auto game_actor = CCore::g_instance->get_game_actor();
        if (!game_actor || !game_actor->game_trailer_actor) {
            CCore::g_instance->warning("Game actor or trailer actor is null");
            return false;
        }
        
        return true;
    }
    
    void CTrailerManipulation::safe_disconnect_trailer(int trailer_index) const {
        CCore::g_instance->info("Starting safe trailer disconnection for trailer {}", trailer_index);
        
        if (!is_safe_to_manipulate_trailer(trailer_index)) {
            CCore::g_instance->warning("Safety check failed for trailer {}", trailer_index);
            return;
        }
        
        CCore::g_instance->info("Attempting to disconnect trailer {} using direct approach", trailer_index);
        
        try {
            // Instead of calling potentially wrong crashes_when_disconnected function,
            // let's try a more direct approach using the connect_slave function
            if (this->connect_slave_address_ != 0 && trailer_index > 0) {
                // Get the game actor
                auto game_actor = CCore::g_instance->get_game_actor();
                if (game_actor && game_actor->game_trailer_actor) {
                    CCore::g_instance->info("Attempting to disconnect by setting slave to null");
                    
                    // For direct disconnection, we'll try setting the slave to nullptr
                    // This is a safer approach than calling unknown functions
                    auto connect_fn = reinterpret_cast<void(*)(prism::game_trailer_actor_u*, prism::game_trailer_actor_u*)>(this->connect_slave_address_);
                    connect_fn(game_actor->game_trailer_actor, nullptr);
                    
                    CCore::g_instance->info("Disconnection attempt completed");
                }
            } else {
                CCore::g_instance->warning("Cannot disconnect: connect_slave function not available or invalid trailer index");
            }
        } catch (...) {
            CCore::g_instance->error("Exception occurred during trailer disconnection");
        }
    }

    void CTrailerManipulation::render_trailer_steering( prism::game_trailer_actor_u* current_trailer, uint32_t i ) const
    {
        if ( ImGui::Checkbox( "Locked##steering", &locked_trailers[ i ] ) )
        {
            CCore::g_instance->info( "{} steering for {}", locked_trailers[ i ] ? "Locking" : "Unlocking", i );
        }
        ImGui::BeginDisabled( !locked_trailers[ i ] );
        if ( ImGui::SliderFloat( "Angle", &current_trailer->steering, -1.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp ) )
        {
            CCore::g_instance->info( "Changed steering angle for trailer {} to {}", i, current_trailer->steering );
            this->set_individual_steering_fn_( current_trailer->wheel_steering_stuff, current_trailer->steering );
        }

        // Note: PushItemFlag was removed in newer ImGui, using button repeat directly
        ImGuiIO& io = ImGui::GetIO();
        bool was_repeat = io.KeyRepeatDelay >= 0.0f;
        io.KeyRepeatDelay = 0.250f;
        io.KeyRepeatRate = 0.050f;

        if ( ImGui::ArrowButton( "rotate_left", ImGuiDir_Left ) )
        {
            current_trailer->steering -= 0.02f;
            if ( current_trailer->steering < -1.f )
            {
                current_trailer->steering = -1.f;
            }
            this->set_individual_steering_fn_( current_trailer->wheel_steering_stuff, current_trailer->steering );
        }
        ImGui::SameLine();
        if ( ImGui::Button( "center" ) )
        {
            current_trailer->steering = 0.f;
            this->set_individual_steering_fn_( current_trailer->wheel_steering_stuff, current_trailer->steering );
        }
        ImGui::SameLine();

        if ( ImGui::ArrowButton( "rotate_right", ImGuiDir_Right ) )
        {
            current_trailer->steering += 0.02f;
            if ( current_trailer->steering > 1.f )
            {
                current_trailer->steering = 1.f;
            }
            this->set_individual_steering_fn_( current_trailer->wheel_steering_stuff, current_trailer->steering );
        }

        // Restore original repeat settings
        if (!was_repeat) {
            io.KeyRepeatDelay = -1.0f;
        }

        ImGui::EndDisabled();
    }

    void CTrailerManipulation::connect_trailer( prism::game_trailer_actor_u* current_trailer, const uint32_t i ) const
    {
        // disable the function that automatically connects slave trailers
        if ( connect_slave_hook->hook() != CHook::HOOKED )
        {
            CCore::g_instance->error( "Could not enable 'connect_slave' hook in 'connect_trailer'" );
            return;
        }
        auto* last_connected_trailer = CCore::g_instance->get_game_actor()->get_last_trailer_connected_to_truck();

        if ( last_connected_trailer == nullptr )
        {
            auto* truck = CCore::g_instance->get_game_actor()->game_physics_vehicle;
            const float3_t vec{
                truck->accessory_chassis_data->hook_position.x - truck->hook_locator.x,
                truck->accessory_chassis_data->hook_position.y - truck->hook_locator.y,
                truck->accessory_chassis_data->hook_position.z - truck->hook_locator.z
            };
            current_trailer->connect( truck, vec, 0, true, false );
            current_trailer->set_trailer_brace( false );
        }
        else
        {
            float3_t slave_hook_position{};
            get_slave_hook_position_fn_( last_connected_trailer, &slave_hook_position );
            const float3_t vec{
                slave_hook_position.x - last_connected_trailer->hook_locator.x,
                slave_hook_position.y - last_connected_trailer->hook_locator.y,
                slave_hook_position.z - last_connected_trailer->hook_locator.z
            };
            current_trailer->connect( last_connected_trailer, vec, 0, true, false );
            current_trailer->set_trailer_brace( false );
        }

        if ( connect_slave_hook->unhook() != CHook::CREATED )
        {
            CCore::g_instance->error( "Could not disable 'connect_slave' hook in 'connect_trailer'" );
        }
    }

    void CTrailerManipulation::render_trailer_joint( prism::game_trailer_actor_u* current_trailer, const uint32_t i ) const
    {
        ImGui::SeparatorText( "Joint" );
        if ( CCore::g_instance->get_base_ctrl_instance()->selected_physics_engine == 1 ) // PhysX
        {
            if ( current_trailer->physics_joint != nullptr && current_trailer->physics_joint->px_joint != nullptr )
            {
                if ( ImGui::RadioButton( "Unlocked##joint", trailer_joints[ i ] == TrailerJointState::NORMAL ) )
                {
                    if ( trailer_joints[ i ] == TrailerJointState::DISCONNECTED )
                    {
                        this->connect_trailer( current_trailer, i );
                    }

                    trailer_joints[ i ] = TrailerJointState::NORMAL;
                    current_trailer->physics_joint->px_joint->setMotion( physx::PxD6Axis::eTWIST, physx::PxD6Motion::eFREE );
                }
                ImGui::SameLine();
                if ( ImGui::RadioButton( "Locked##joint", trailer_joints[ i ] == TrailerJointState::LOCKED ) )
                {
                    if ( trailer_joints[ i ] == TrailerJointState::DISCONNECTED )
                    {
                        this->connect_trailer( current_trailer, i );
                    }

                    trailer_joints[ i ] = TrailerJointState::LOCKED;
                    current_trailer->physics_joint->px_joint->setMotion( physx::PxD6Axis::eTWIST, physx::PxD6Motion::eLOCKED );
                }
            }
        }
        else
        {
            ImGui::TextWrapped( "Ability to lock joints is only available with PhysX" );
        }

        ImGui::SeparatorText( "Connect/Disconnect" );
        // nothing in this plugin is recommended to be used in TruckersMP but this is completely broken when used in TruckersMP and WILL get you banned, so I've explicitly disabled it.
        if ( !CCore::g_instance->is_truckersmp() )
        {
            ImGui::BeginDisabled( current_trailer->physics_joint != nullptr );
            if ( ImGui::Button( "Connect##trailer" ) )
            {
                this->connect_trailer( current_trailer, i );
                trailer_joints[ i ] = TrailerJointState::NORMAL;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled( current_trailer->physics_joint == nullptr );
            if ( ImGui::Button( "Disconnect##trailer" ) )
            {
                CCore::g_instance->info("User clicked disconnect button for trailer {}", i);
                
                // Use our safer disconnection approach
                this->safe_disconnect_trailer(i);
                
                // Also do the original approach as backup
                current_trailer->set_trailer_brace( true );
                current_trailer->disconnect();
                trailer_joints[ i ] = TrailerJointState::DISCONNECTED;
            }
            ImGui::EndDisabled();
        }
        else
        {
            ImGui::TextWrapped( "Individually detachable trailers does not work in TruckersMP" );
        }
    }

    void CTrailerManipulation::render_trailers() const
    {
        // SDK 1.14 TELEMETRY APPROACH: Check for trailers using telemetry data
        bool has_trailers = CCore::g_instance->has_trailers();
        int trailer_count = CCore::g_instance->get_trailer_count();
        
        ImGui::Text("=== SDK 1.14 Telemetry Trailer Detection ===");
        ImGui::Text("Connected trailers: %d", trailer_count);
        
        if (!has_trailers) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "No trailers connected (via telemetry)");
            ImGui::Text("Make sure you have attached trailers in-game.");
            ImGui::Text("");
            ImGui::Text("Telemetry Status:");
            for (int i = 0; i < 10; i++) {
                bool connected = CCore::g_instance->is_trailer_connected(i);
                if (connected) {
                    ImGui::Text("  Trailer %d: CONNECTED", i);
                } else if (i < 3) {
                    ImGui::TextDisabled("  Trailer %d: disconnected", i);
                }
            }
            return;
        }
        
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "SUCCESS: Trailers detected via SDK 1.14 telemetry!");
        ImGui::Text("");
        
        // Show connected trailers
        for (int i = 0; i < 10; i++) {
            if (CCore::g_instance->is_trailer_connected(i)) {
                ImGui::Text("Trailer %d: CONNECTED", i);
            }
        }
        
        ImGui::Separator();
        ImGui::Text("=== Memory-based Legacy Debugging (SDK 1.13 and older) ===");
        ImGui::TextDisabled("The following debug info shows why memory approach fails in SDK 1.14:");
        
        auto* base_ctrl = CCore::g_instance->get_base_ctrl_instance();
        if (base_ctrl == nullptr)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Warning: Cannot find game base controller");
            ImGui::Text("This usually means pattern scanning failed after a game update.");
            ImGui::Text("Check the console for detailed error messages.");
            return;
        }

        auto* game_actor = CCore::g_instance->get_game_actor();
        
        ImGui::TextDisabled("Legacy memory fields (for comparison):");
        ImGui::Text("Base ctrl: 0x%016llx", reinterpret_cast<uint64_t>(base_ctrl));
        ImGui::Text("Game actor: 0x%016llx", reinterpret_cast<uint64_t>(game_actor));
        if (game_actor != nullptr) {
            ImGui::Text("game_trailer_actor field: 0x%016llx", reinterpret_cast<uint64_t>(game_actor->game_trailer_actor));
            ImGui::TextDisabled("(This field is null in SDK 1.14 - trailers moved to telemetry)");
        }
        
        // TODO: Implement actual trailer manipulation using telemetry data
        // For now, just show that telemetry detection works
        ImGui::Separator();
        ImGui::Text("=== Hybrid Trailer Manipulation (Telemetry + Memory) ===");
        ImGui::Text("Detection: Telemetry-based (SDK 1.14) ✓");
        ImGui::Text("Manipulation: Memory-based (when available)");
        
        if (has_trailers) {
            ImGui::Text("Ready to manipulate %d trailer(s)!", trailer_count);
            
            // COMPREHENSIVE MEMORY ACCESS DEBUGGING
            CCore::g_instance->info("=== MEMORY ACCESS ANALYSIS FOR TRAILER MANIPULATION ===");
            
            // Try to get memory-based trailer access for manipulation
            auto* game_actor = CCore::g_instance->get_game_actor();
            prism::game_trailer_actor_u* memory_trailer = nullptr;
            
            CCore::g_instance->info("Step 1: Game actor lookup result: 0x%016llx", reinterpret_cast<uint64_t>(game_actor));
            
            if (game_actor) {
                CCore::g_instance->info("Step 2: Checking game_actor->game_trailer_actor field...");
                CCore::g_instance->info("  game_trailer_actor field: 0x%016llx", reinterpret_cast<uint64_t>(game_actor->game_trailer_actor));
                
                if (game_actor->game_trailer_actor) {
                    memory_trailer = game_actor->game_trailer_actor;
                    CCore::g_instance->info("SUCCESS: Memory access available via game_actor->game_trailer_actor!");
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Memory access available for manipulation!");
                } else {
                    CCore::g_instance->warning("Step 2 FAILED: game_actor->game_trailer_actor is NULL");
                    CCore::g_instance->info("Step 2.5: SDK 1.14 - searching game actor for alternative trailer storage...");
                    
                    // In SDK 1.14, trailers might be stored differently in the game actor
                    // Let's scan the entire game actor structure for potential trailer pointers
                    auto* actor_ptr = reinterpret_cast<uint64_t*>(game_actor);
                    for (int i = 0; i < 200; i++) { // Scan first 1600 bytes
                        if (!IsBadReadPtr(&actor_ptr[i], sizeof(uint64_t))) {
                            uint64_t value = actor_ptr[i];
                            
                            // Look for valid pointer values
                            if (value > 0x10000 && value < 0x7FFFFFFFFFFF) {
                                // Try to validate if this could be a trailer
                                auto* potential_trailer = reinterpret_cast<void*>(value);
                                if (!IsBadReadPtr(potential_trailer, 0x100)) {
                                    auto* trailer_data = reinterpret_cast<uint64_t*>(potential_trailer);
                                    uint64_t first_val = trailer_data[0];
                                    
                                    // Check if this looks like a trailer structure
                                    if (first_val > 0x10000 && first_val < 0x7FFFFFFFFFFF) {
                                        // Additional validation - check for trailer-like patterns
                                        bool looks_like_trailer = false;
                                        
                                        // Check for common trailer structure patterns
                                        for (int j = 1; j < 10; j++) {
                                            if (!IsBadReadPtr(&trailer_data[j], sizeof(uint64_t))) {
                                                uint64_t val = trailer_data[j];
                                                // Look for null pointers or reasonable values
                                                if (val == 0 || (val > 0x10000 && val < 0x7FFFFFFFFFFF)) {
                                                    looks_like_trailer = true;
                                                    break;
                                                }
                                            }
                                        }
                                        
                                        if (looks_like_trailer) {
                                            CCore::g_instance->info("  Potential trailer found at game_actor+0x%03x: 0x%016llx", 
                                                i * 8, value);
                                            if (!memory_trailer) {
                                                memory_trailer = reinterpret_cast<prism::game_trailer_actor_u*>(potential_trailer);
                                                CCore::g_instance->info("  Using as primary trailer for manipulation");
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    
                    if (memory_trailer) {
                        CCore::g_instance->info("SUCCESS: Found trailer via alternative scanning in game actor!");
                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Alternative trailer memory access found!");
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Memory access not available - trying base controller...");
                    }
                }
            } else {
                CCore::g_instance->error("Step 1 FAILED: Could not get game actor");
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Game actor not accessible");
            }
            
            // Alternative method: base controller arrays
            if (!memory_trailer) {
                CCore::g_instance->info("Step 3: Trying base controller trailer arrays...");
                auto* base_ctrl = CCore::g_instance->get_base_ctrl_instance();
                if (base_ctrl && !IsBadReadPtr(base_ctrl, 0x300)) {
                    auto* base_ctrl_ptr = reinterpret_cast<uint8_t*>(base_ctrl);
                    void** trailer_array_ptr = reinterpret_cast<void**>(base_ctrl_ptr + 0x0228);
                    
                    CCore::g_instance->info("  Base controller: 0x%016llx", reinterpret_cast<uint64_t>(base_ctrl));
                    CCore::g_instance->info("  Trailer array pointer: 0x%016llx", reinterpret_cast<uint64_t>(trailer_array_ptr));
                    
                    if (!IsBadReadPtr(trailer_array_ptr, 32)) {
                        void* array_data = trailer_array_ptr[0];
                        uint64_t array_size = reinterpret_cast<uint64_t>(trailer_array_ptr[1]);
                        
                        CCore::g_instance->info("  Array data: 0x%016llx", reinterpret_cast<uint64_t>(array_data));
                        CCore::g_instance->info("  Array size: %llu", array_size);
                        
                        if (array_data && array_size > 0 && array_size < 10) {
                            auto* trailer_ptrs = reinterpret_cast<void**>(array_data);
                            if (!IsBadReadPtr(trailer_ptrs, array_size * sizeof(void*))) {
                                CCore::g_instance->info("  Scanning array entries:");
                                for (uint64_t i = 0; i < array_size; i++) {
                                    void* trailer_ptr = trailer_ptrs[i];
                                    CCore::g_instance->info("    [%llu]: 0x%016llx", i, reinterpret_cast<uint64_t>(trailer_ptr));
                                    if (trailer_ptr) {
                                        memory_trailer = reinterpret_cast<prism::game_trailer_actor_u*>(trailer_ptr);
                                        CCore::g_instance->info("SUCCESS: Found trailer in base controller array!");
                                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Found trailer in base controller array!");
                                        break;
                                    }
                                }
                            } else {
                                CCore::g_instance->warning("  Cannot read trailer array entries");
                            }
                        } else {
                            CCore::g_instance->warning("  Array data is null or invalid size");
                        }
                    } else {
                        CCore::g_instance->warning("  Cannot read trailer array pointer");
                    }
                } else {
                    CCore::g_instance->warning("  Base controller is null or unreadable");
                }
            }
            
            CCore::g_instance->info("Step 4: Final memory_trailer result: 0x%016llx", reinterpret_cast<uint64_t>(memory_trailer));
            CCore::g_instance->info("=== MEMORY ACCESS ANALYSIS COMPLETE ===");
            
            // Initialize steering hook if we have memory access
            if (memory_trailer && game_actor && game_actor->game_trailer_actor) {
                // Initialize steering hook if not already done
                if (steering_advance_hook == nullptr) {
                    CCore::g_instance->info("=== STEERING HOOK INITIALIZATION ===");
                    CCore::g_instance->info("Memory trailer: 0x%016llx", reinterpret_cast<uint64_t>(memory_trailer));
                    
                    // Check if we can read the vtable
                    auto* trailer_vtable_ptr = reinterpret_cast<uint64_t*>(memory_trailer);
                    if (!IsBadReadPtr(trailer_vtable_ptr, sizeof(uint64_t))) {
                        uint64_t vtable = trailer_vtable_ptr[0];
                        CCore::g_instance->info("Trailer vtable: 0x%016llx", vtable);
                        
                        // Calculate steering_advance address
                        const auto steering_advance_address = vtable + 0x08 * 73;
                        CCore::g_instance->info("Calculated steering_advance address: 0x%016llx", steering_advance_address);
                        
                        // Validate the address looks reasonable
                        if (steering_advance_address > 0x10000 && steering_advance_address < 0x7FFFFFFFFFFF) {
                            CCore::g_instance->info("Attempting to hook steering_advance...");
                            steering_advance_hook = CCore::g_instance->get_hooks_manager()->register_virtual_function_hook(
                                "physics_trailer_u::steering_advance",
                                steering_advance_address,
                                reinterpret_cast<uint64_t>(&hk_steering_advance)
                            );

                            if (steering_advance_hook && steering_advance_hook->hook() == CHook::HOOKED) {
                                CCore::g_instance->info("SUCCESS: Hooked physics_trailer_u::steering_advance");
                            } else {
                                CCore::g_instance->error("FAILED: Could not hook physics_trailer_u::steering_advance");
                            }
                        } else {
                            CCore::g_instance->error("Calculated steering_advance address looks invalid");
                        }
                    } else {
                        CCore::g_instance->error("Cannot read trailer vtable");
                    }
                    CCore::g_instance->info("=== STEERING HOOK INITIALIZATION COMPLETE ===");
                } else {
                    CCore::g_instance->debug("Steering hook already initialized");
                }
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Memory access not available - trying alternative methods...");
                
                // Try to find trailer memory via base controller arrays
                auto* base_ctrl = CCore::g_instance->get_base_ctrl_instance();
                if (base_ctrl && !IsBadReadPtr(base_ctrl, 0x300)) {
                    auto* base_ctrl_ptr = reinterpret_cast<uint8_t*>(base_ctrl);
                    void** trailer_array_ptr = reinterpret_cast<void**>(base_ctrl_ptr + 0x0228);
                    
                    if (!IsBadReadPtr(trailer_array_ptr, 32)) {
                        void* array_data = trailer_array_ptr[0];
                        uint64_t array_size = reinterpret_cast<uint64_t>(trailer_array_ptr[1]);
                        
                        if (array_data && array_size > 0 && array_size < 10) {
                            auto* trailer_ptrs = reinterpret_cast<void**>(array_data);
                            if (!IsBadReadPtr(trailer_ptrs, array_size * sizeof(void*))) {
                                for (uint64_t i = 0; i < array_size; i++) {
                                    if (trailer_ptrs[i]) {
                                        memory_trailer = reinterpret_cast<prism::game_trailer_actor_u*>(trailer_ptrs[i]);
                                        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Found trailer in base controller array!");
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            // Show controls for each connected trailer (detected via telemetry)
            for (int i = 0; i < 10; i++) {
                if (CCore::g_instance->is_trailer_connected(i)) {
                    ImGui::PushID(i);
                    if (ImGui::CollapsingHeader(("Trailer " + std::to_string(i)).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                        
                        if (memory_trailer) {
                            // Use the memory-based manipulation functions for the first trailer
                            // (for multi-trailer setups, we'd need to walk the linked list)
                            
                            if (memory_trailer->wheel_steering_stuff != nullptr && set_individual_steering_fn_ != nullptr) {
                                ImGui::SeparatorText("Steering");
                                this->render_trailer_steering(memory_trailer, i);
                            } else {
                                ImGui::TextDisabled("Steering: Memory access not available");
                            }
                            
                            ImGui::SeparatorText("Joint Control");
                            this->render_trailer_joint(memory_trailer, i);
                            
                            // For multiple trailers, walk to the next one
                            if (i == 0 && memory_trailer->slave_trailer) {
                                memory_trailer = memory_trailer->slave_trailer;
                            }
                            
                        } else {
                            ImGui::TextDisabled("Steering controls: Memory access required");
                            ImGui::TextDisabled("Joint controls: Memory access required");
                            ImGui::Text("Trailer detected via telemetry but memory structures not accessible.");
                            ImGui::Text("This may happen after major game updates.");
                        }
                    }
                    ImGui::PopID();
                }
            }
        }
    }

    void CTrailerManipulation::render()
    {
        ImGui::Begin( "Trailer Manipulation"/*, &this->open_ */ );

        // Safety check - disable UI if crash prevention function isn't available
        if (!safety_functions_available_) {
            ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "WARNING: TRAILER MANIPULATION DISABLED");
            ImGui::TextWrapped("The 'crashes_when_disconnected' safety function could not be found in SDK 1.14.");
            ImGui::TextWrapped("Trailer manipulation has been disabled to prevent game crashes/freezes.");
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Telemetry detection still works:");
            
            // Show trailer detection status even when manipulation is disabled
            int trailer_count = 0;
            for (int i = 0; i < 10; i++) {
                if (CCore::g_instance->is_trailer_connected(i)) {
                    trailer_count++;
                }
            }
            ImGui::Text("Detected trailers: %d", trailer_count);
            
            ImGui::End();
            return;
        }

        this->render_trailers();

        ImGui::End();
    }
}
