#include "reaper_api.h"
#define REAPERAPI_IMPLEMENT
#include <reaper_plugin_functions.h>

#include "llm.hpp"

extern "C" {
REAPER_PLUGIN_DLL_EXPORT int ReaperPluginEntry(
    REAPER_PLUGIN_HINSTANCE hInstance,
    reaper_plugin_info_t* rec)
{
    (void)hInstance;
    if (!rec) {
        llm::Register(false);
        return 0;
    }
    else if (
        rec->caller_version != REAPER_PLUGIN_VERSION ||
        REAPERAPI_LoadAPI(rec->GetFunc)) {
        return 0;
    }

    llm::Register(true);

    return 1;
}
}
