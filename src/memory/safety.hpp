#pragma once
#include <Windows.h>
#include <functional>

namespace ts_extra_utilities::safety
{
    class SafeExecutor
    {
    public:
        template<typename T>
        static T safe_call(std::function<T()> func, T default_value = T{}, const char* operation_name = "operation")
        {
            __try
            {
                return func();
            }
            __except(EXCEPTION_EXECUTE_HANDLER)
            {
                // Log the exception if possible
                if (CCore::g_instance)
                {
                    CCore::g_instance->error("Exception caught during {}: 0x{:08x}", 
                        operation_name, GetExceptionCode());
                }
                return default_value;
            }
        }

        static bool safe_call_void(std::function<void()> func, const char* operation_name = "operation")
        {
            __try
            {
                func();
                return true;
            }
            __except(EXCEPTION_EXECUTE_HANDLER)
            {
                // Log the exception if possible
                if (CCore::g_instance)
                {
                    CCore::g_instance->error("Exception caught during {}: 0x{:08x}", 
                        operation_name, GetExceptionCode());
                }
                return false;
            }
        }
    };
}