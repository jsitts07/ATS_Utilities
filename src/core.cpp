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

        this->info( "Found base_ctrl @ +{:x}, game_actor_offset: +{:x}", 
            memory::as_offset( this->base_ctrl_instance_ptr_address ),
            this->game_actor_offset_in_base_ctrl );

        return *reinterpret_cast< prism::base_ctrl_u** >( this->base_ctrl_instance_ptr_address );
    }

    prism::game_actor_u* CCore::get_game_actor()
    {
        auto* base_ctrl = this->get_base_ctrl_instance();

        if ( base_ctrl == nullptr || this->game_actor_offset_in_base_ctrl == 0 )
        {
            return nullptr;
        }

        return *reinterpret_cast< prism::game_actor_u** >( reinterpret_cast< uint64_t >( base_ctrl ) + this->game_actor_offset_in_base_ctrl );
    }
}
