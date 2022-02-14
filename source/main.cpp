#include "llm.hpp"
#include "node.hpp"
#define REAPERAPI_IMPLEMENT
#include <reaper_plugin_functions.h>

extern "C" {
REAPER_PLUGIN_DLL_EXPORT int ReaperPluginEntry(
    REAPER_PLUGIN_HINSTANCE hInstance,
    reaper_plugin_info_t* rec)
{
    (void)hInstance;
    if (!rec) {
        // llm::Register(false);
        return 0;
    }
    else if (
        rec->caller_version != REAPER_PLUGIN_VERSION ||
        REAPERAPI_LoadAPI(rec->GetFunc)) {
        return 0;
    }
    // llm::Register(true);
    MediaTrack* tr = nullptr;
    Node<MediaTrack*, GUID*, int> n {tr};
    n.neighborhood();
    (void)tr;
    (void)n;
    return 1;
}
}
