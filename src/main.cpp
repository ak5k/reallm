#define REAPERAPI_IMPLEMENT
#include <reaper_plugin_functions.h>

#include "reallm2.hpp"

extern "C"
{
REAPER_PLUGIN_DLL_EXPORT auto REAPER_PLUGIN_ENTRYPOINT(
  REAPER_PLUGIN_HINSTANCE hInstance, reaper_plugin_info_t* rec) -> int
{
  if (rec != nullptr && REAPERAPI_LoadAPI(rec->GetFunc) == 0)
  {
    reallm2::Register();
    return 1;
  }
  // quit
  return 0;
}
}