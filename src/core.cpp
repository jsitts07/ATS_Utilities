#include "core.hpp"

#include <MinHook.h>

#include "consts.hpp"

#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include "memory/memory_utils.hpp"
#include "memory/robust_pattern_scanner.hpp"
#include "debug/debug_helpers.hpp"

#include "managers/window_manager.hpp"
#include "windows/trailer_manipulation.hpp"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND, UINT, WPARAM, LPARAM );

namespace ts_extra_utilities
{
    CCore* CCore::g_instance = nullptr;

    CCore::CCore( const scs_telemetry_init_params_v101_t* init_params ) : init_params_( init_params )
    {
        this->hooks_manager_ = new CHooksManager();
        this->window_manager_ = new CWindowManager();
        scs_log_ = init_params->common.log;
        g_instance = this;
    }

    CCore::~CCore()
    {
        try {
            this->destroy();
            
            // Safely uninitialize MinHook
            if (MH_Uninitialize() != MH_OK) {
                // MinHook may already be uninitialized, which is fine
            }
        } catch (...) {
            // Prevent exceptions during destruction
        }
    }

    bool CCore::init()
    {
        try {
            // Basic SCS logging first
            this->info("TS-Extra-Utilities: Starting initialization...");
            
            MH_Initialize();
            
            this->info("TS-Extra-Utilities: MinHook initialized");
            
            truckersmp_ = GetModuleHandle( L"core_ets2mp.dll" ) != nullptr || GetModuleHandle( L"core_atsmp.dll" ) != nullptr;

            // Try to initialize DirectX11 hook
            this->dx11_hook = new CDirectX11Hook();
            if ( !this->dx11_hook->hook_present() )
            {
                this->error("TS-Extra-Utilities: Failed to hook DirectX11 present function");
                return false;
            }
            
            this->info("TS-Extra-Utilities: DirectX11 hooked successfully");
            
            // Try to initialize DirectInput8 hook  
            this->di8_hook = new CDirectInput8Hook();
            if ( !this->di8_hook->hook() )
            {
                this->error("TS-Extra-Utilities: Failed to hook DirectInput8");
                return false;
            }
            
            this->info("TS-Extra-Utilities: DirectInput8 hooked successfully");

            // Initialize debug helpers AFTER basic hooks are working
            debug::CrashHandler::initialize();
            debug::DebugLogger::init();
            
            this->info("TS-Extra-Utilities: Debug helpers initialized");

            // Register telemetry callbacks for trailer detection (SDK 1.14 approach)
            this->info("TS-Extra-Utilities: Registering trailer telemetry callbacks...");
            if (init_params_ && init_params_->register_for_channel) {
                // Register for each trailer's connected state (trailer.0.connected to trailer.9.connected)
                for (int i = 0; i < MAX_TRAILERS; i++) {
                    char channel_name[64];
                    snprintf(channel_name, sizeof(channel_name), "trailer.%d.connected", i);
                    
                    scs_result_t result = init_params_->register_for_channel(
                        channel_name, 
                        SCS_U32_NIL,  // Use SCS_U32_NIL for non-array channels
                        SCS_VALUE_TYPE_bool,  // trailer.X.connected is a boolean value
                        SCS_TELEMETRY_CHANNEL_FLAG_none,  // no special flags
                        trailer_connected_callback, 
                        this
                    );
                    if (result == SCS_RESULT_ok) {
                        this->info("TS-Extra-Utilities: Registered for %s", channel_name);
                    } else {
                        this->warning("TS-Extra-Utilities: Failed to register for %s (result: %d)", channel_name, result);
                    }
                }
                this->info("TS-Extra-Utilities: Trailer telemetry registration complete");
            } else {
                this->error("TS-Extra-Utilities: Cannot register telemetry - init_params or register_for_channel is null");
            }

            const auto trailer_manipulation = this->window_manager_->register_window( std::make_shared< CTrailerManipulation >() );

            if ( !trailer_manipulation->init() )
            {
                this->error("TS-Extra-Utilities: Could not initialize the trailer manipulation module");
                // Don't return false here - let the mod continue without trailer features
            }
            else
            {
                this->info("TS-Extra-Utilities: Trailer manipulation module initialized successfully");
            }

            this->info("TS-Extra-Utilities: Initialization completed successfully");
            return true;
        } catch (const std::exception& e) {
            this->error("TS-Extra-Utilities: Exception during initialization: %s", e.what());
            return false;
        } catch (...) {
            this->error("TS-Extra-Utilities: Unknown exception during initialization");
            return false;
        }
    }

    void CCore::destroy()
    {
        debug::DebugLogger::info("Shutting down ATS mod...");
        
        // Safely cleanup hooks and managers
        if (this->dx11_hook) {
            delete this->dx11_hook;
            this->dx11_hook = nullptr;
        }
        if (this->di8_hook) {
            delete this->di8_hook;
            this->di8_hook = nullptr;
        }
        if (this->hooks_manager_) {
            delete this->hooks_manager_;
            this->hooks_manager_ = nullptr;
        }
        if (this->window_manager_) {
            delete this->window_manager_;
            this->window_manager_ = nullptr;
        }
        
        debug::CrashHandler::shutdown();
        debug::DebugLogger::info("ATS mod shutdown completed");
    }

    bool CCore::on_mouse_input( LPDIDEVICEOBJECTDATA rgdod )
    {
        if ( !this->disable_in_game_mouse ) return false;

        auto& io = ImGui::GetIO();
        if ( rgdod->dwOfs == DIMOFS_X )
        {
            io.MousePos.x += static_cast< float >( static_cast< int >( rgdod->dwData ) );
        }
        else if ( rgdod->dwOfs == DIMOFS_Y )
        {
            io.MousePos.y += static_cast< float >( static_cast< int >( rgdod->dwData ) );
        }

        return true;
    }

    bool CCore::render()
    {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if ( this->render_ui )
        {
#ifdef _DEBUG
            bool s = true;
            ImGui::ShowDemoWindow( &s );
#endif
            this->window_manager_->render();
        }
        ImGui::EndFrame();
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData( ImGui::GetDrawData() );
        return true;
    }

    void CCore::toggle_input_hook()
    {
        this->disable_in_game_mouse = !this->disable_in_game_mouse;
        auto& io = ImGui::GetIO();
        if ( this->disable_in_game_mouse )
        {
            io.MousePos.x = last_mouse_pos_x_;
            io.MousePos.y = last_mouse_pos_y_;
        }
        else
        {
            last_mouse_pos_x_ = io.MousePos.x;
            last_mouse_pos_y_ = io.MousePos.y;
        }

        io.MouseDrawCursor = this->disable_in_game_mouse;

        this->debug( "Mouse hook is now {}", this->disable_in_game_mouse ? "active" : "disabled" );
    }

    void CCore::toggle_ui()
    {
        this->render_ui = !this->render_ui;
    }

    // TODO: add keybind settings
    bool CCore::on_wnd_proc( HWND hwnd, UINT umsg, WPARAM wparam, LPARAM lparam )
    {
        if ( umsg == WM_KEYDOWN )
        {
            if ( wparam == VK_INSERT )
            {
                toggle_input_hook();
                return true;
            }
            if ( wparam == VK_DELETE )
            {
                toggle_ui();
                return true;
            }
        }
        else if ( umsg == WM_MOUSEMOVE )
        {
            if ( this->disable_in_game_mouse )
            {
                return true;
            }
        }

        if ( ImGui_ImplWin32_WndProcHandler( hwnd, umsg, wparam, lparam ) )
        {
            return true;
        }
        return false;
    }

    prism::base_ctrl_u* CCore::get_base_ctrl_instance()
    {
        if ( this->base_ctrl_instance_ptr_address != 0 ) 
        {
            auto* base_ctrl = *reinterpret_cast< prism::base_ctrl_u** >( this->base_ctrl_instance_ptr_address );
            if (base_ctrl != nullptr)
                return base_ctrl;
                
            // If cached address now returns null, invalidate cache and rescan
            this->warning("Cached base_ctrl address now returns null, rescanning...");
            this->base_ctrl_instance_ptr_address = 0;
        }

        const auto addr = pattern_scanner::RobustPatternScanner::find_with_fallbacks(
            "base_ctrl_instance", 
            pattern_scanner::patterns::BASE_CTRL_PATTERNS
        );

        if ( addr == 0 ) 
        {
            this->error("Could not find base_ctrl_instance - game may have updated");
            return nullptr;
        }
        
        this->base_ctrl_instance_ptr_address = addr + *reinterpret_cast< int32_t* >( addr + 3 ) + 7;
        this->game_actor_offset_in_base_ctrl = *reinterpret_cast< int32_t* >( addr + 14 );

        this->info( "Found base_ctrl @ +0x%llx, game_actor_offset: +0x%llx", 
            memory::as_offset( this->base_ctrl_instance_ptr_address ),
            this->game_actor_offset_in_base_ctrl );
        
        // Log detailed base controller information
        auto* base_ctrl_result = *reinterpret_cast< prism::base_ctrl_u** >( this->base_ctrl_instance_ptr_address );
        this->info( "Base controller pointer: 0x%016llx", reinterpret_cast<uint64_t>(base_ctrl_result) );
        
        if (base_ctrl_result && !IsBadReadPtr(base_ctrl_result, 0x400)) {
            this->info( "Base controller first few values:" );
            auto* base_ptr = reinterpret_cast<uint64_t*>(base_ctrl_result);
            for (int i = 0; i < 8; i++) {
                this->info( "  +0x%03x: 0x%016llx", i * 8, base_ptr[i] );
            }
        }

        return base_ctrl_result;
    }

    prism::game_actor_u* CCore::get_game_actor()
    {
        this->debug("=== GAME ACTOR LOOKUP START ===");
        auto* base_ctrl = this->get_base_ctrl_instance();
        if (base_ctrl == nullptr) {
            this->warning("Base controller is null, cannot get game actor");
            return nullptr;
        }
        this->debug("Base controller valid: 0x%016llx", reinterpret_cast<uint64_t>(base_ctrl));

        // Validate the cached offset first
        if (this->game_actor_offset_in_base_ctrl != 0) {
            this->debug("Trying cached offset: 0x%x", this->game_actor_offset_in_base_ctrl);
            auto* potential_actor = reinterpret_cast<prism::game_actor_u**>(
                reinterpret_cast<uint64_t>(base_ctrl) + this->game_actor_offset_in_base_ctrl
            );
            
            this->debug("Potential actor pointer location: 0x%016llx", reinterpret_cast<uint64_t>(potential_actor));
            
            // Validate the pointer looks reasonable
            if (!IsBadReadPtr(potential_actor, sizeof(void*))) {
                auto* actor = *potential_actor;
                this->debug("Actor pointer value: 0x%016llx", reinterpret_cast<uint64_t>(actor));
                
                if (actor && !IsBadReadPtr(actor, 0x100)) {
                    // Additional validation - check if this looks like a game actor
                    auto* actor_ptr = reinterpret_cast<uint64_t*>(actor);
                    uint64_t first_value = actor_ptr[0];
                    this->debug("Actor first value: 0x%016llx", first_value);
                    
                    if (first_value > 0x10000 && first_value < 0x7FFFFFFFFFFF) {
                        this->info("Cached game actor is valid: 0x%016llx", reinterpret_cast<uint64_t>(actor));
                        return actor;
                    } else {
                        this->warning("Cached actor first value looks invalid: 0x%016llx", first_value);
                    }
                } else {
                    this->warning("Cached actor pointer is null or unreadable");
                }
            } else {
                this->warning("Cannot read cached actor pointer location");
            }
            
            // Cached offset is invalid, clear it
            this->warning("Cached game actor offset 0x%x is invalid, rescanning...", this->game_actor_offset_in_base_ctrl);
            this->game_actor_offset_in_base_ctrl = 0;
        }

        // Try to find the game actor using known potential offsets for SDK 1.14
        this->info("Scanning for valid game actor offset...");
        static const uint32_t potential_offsets[] = {
            0x2e8,   // Original SDK 1.13 offset
            0x2f0,   // Alternative
            0x300,   // Next potential
            0x310,   // Further offset
            0x2d8,   // Earlier offset
            0x2c8,   // Even earlier
            0x320,   // Later offset
            0x330,   // Even later
        };

        for (size_t idx = 0; idx < sizeof(potential_offsets) / sizeof(potential_offsets[0]); idx++) {
            uint32_t offset = potential_offsets[idx];
            this->debug("Trying offset %zu/8: +0x%x", idx + 1, offset);
            
            auto* potential_actor = reinterpret_cast<prism::game_actor_u**>(
                reinterpret_cast<uint64_t>(base_ctrl) + offset
            );
            
            this->debug("  Pointer location: 0x%016llx", reinterpret_cast<uint64_t>(potential_actor));
            
            if (!IsBadReadPtr(potential_actor, sizeof(void*))) {
                auto* actor = *potential_actor;
                this->debug("  Actor value: 0x%016llx", reinterpret_cast<uint64_t>(actor));
                
                if (actor && !IsBadReadPtr(actor, 0x100)) {
                    // Validate this looks like a game actor
                    auto* actor_ptr = reinterpret_cast<uint64_t*>(actor);
                    uint64_t first_value = actor_ptr[0];
                    this->debug("  First value: 0x%016llx", first_value);
                    
                    // Check if first value looks like a valid pointer (vtable or similar)
                    if (first_value > 0x10000 && first_value < 0x7FFFFFFFFFFF) {
                        this->info("SUCCESS: Found valid game actor at offset +0x%x: 0x%016llx", 
                            offset, reinterpret_cast<uint64_t>(actor));
                        
                        // Log some actor structure details
                        this->info("Game actor structure preview:");
                        for (int i = 0; i < 10; i++) {
                            uint64_t value = actor_ptr[i];
                            this->info("  +0x%03x: 0x%016llx", i * 8, value);
                        }
                        
                        this->game_actor_offset_in_base_ctrl = offset;
                        this->debug("=== GAME ACTOR LOOKUP SUCCESS ===");
                        return actor;
                    } else {
                        this->debug("  Invalid first value, not a game actor");
                    }
                } else {
                    this->debug("  Actor pointer is null or unreadable");
                }
            } else {
                this->debug("  Cannot read potential actor pointer");
            }
        }

        this->error("FAILED: Could not find valid game actor in base controller after trying all offsets");
        this->debug("=== GAME ACTOR LOOKUP FAILED ===");
        return nullptr;
    }

    // Telemetry callback for trailer connection states (SDK 1.14 approach)
    SCSAPI_VOID CCore::trailer_connected_callback(const scs_string_t name, const scs_u32_t index, const scs_value_t* const value, const scs_context_t context)
    {
        auto* core = static_cast<CCore*>(context);
        if (!core || !value || !name) {
            return;
        }

        // Parse trailer index from channel name (e.g., "trailer.3.connected" -> index 3)
        int trailer_index = -1;
        if (sscanf(name, "trailer.%d.connected", &trailer_index) != 1 || trailer_index < 0 || trailer_index >= MAX_TRAILERS) {
            core->warning("Invalid trailer channel name: %s", name);
            return;
        }

        bool connected = (value->type == SCS_VALUE_TYPE_bool) ? value->value_bool.value : false;
        bool was_connected = core->trailer_connected_[trailer_index];
        
        // Update trailer state
        core->trailer_connected_[trailer_index] = connected;
        
        // Recalculate connected trailer count
        int count = 0;
        for (int i = 0; i < MAX_TRAILERS; i++) {
            if (core->trailer_connected_[i]) {
                count++;
            }
        }
        core->connected_trailer_count_ = count;
        
        // Log changes
        if (connected != was_connected) {
            if (connected) {
                core->info("TRAILER CONNECTED: trailer.%d (total: %d trailers)", trailer_index, count);
            } else {
                core->info("TRAILER DISCONNECTED: trailer.%d (total: %d trailers)", trailer_index, count);
            }
        }
    }
}
