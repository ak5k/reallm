#define REAPERAPI_IMPLEMENT
#include <reaper_plugin_functions.h>

#include "reallm.hpp"
#include <string>

extern "C"
{
REAPER_PLUGIN_DLL_EXPORT auto REAPER_PLUGIN_ENTRYPOINT(
  REAPER_PLUGIN_HINSTANCE hInstance, reaper_plugin_info_t* rec) -> int
{
  (void)hInstance;
  if (rec != nullptr && REAPERAPI_LoadAPI(rec->GetFunc) == 0)
  {
    auto version = std::stod(GetAppVersion());
    if (version < 7.0)
    {
      ShowConsoleMsg("RealLm requires Reaper 7.0 or later. Please update "
                     "Reaper.\n");
      return 0;
    }
    if (rec->GetFunc("Llm_Do"))
    {
      return 0;
    }
    reallm::Register();
    return 1;
  }
  // quit
  return 0;
}
}