#include "reallm.hpp"
#include <stdio.h>
#include <string>

#define REAPERAPI_IMPLEMENT
#include <reaper_plugin_functions.h>

#define REQUIRED_API(name)                                                     \
  {                                                                            \
    (void**)&name, #name, true                                                 \
  }
#define OPTIONAL_API(name)                                                     \
  {                                                                            \
    (void**)&name, #name, false                                                \
  }

static bool loadAPI(void* (*getFunc)(const char*))
{
  if (!getFunc)
    return false;

  struct ApiFunc
  {
    void** ptr;
    const char* name;
    bool required;
  };

  const ApiFunc funcs[]{
    REQUIRED_API(Audio_IsRunning),
    REQUIRED_API(GetAppVersion),
    REQUIRED_API(GetInputOutputLatency),
    REQUIRED_API(GetGlobalAutomationOverride),
    REQUIRED_API(GetMasterTrack),
    REQUIRED_API(GetMediaTrackInfo_Value),
    REQUIRED_API(GetNumTracks),
    REQUIRED_API(GetProjExtState),
    REQUIRED_API(GetSetMediaTrackInfo),
    REQUIRED_API(GetTrack),
    REQUIRED_API(GetTrackAutomationMode),
    REQUIRED_API(GetTrackNumSends),
    REQUIRED_API(GetTrackSendInfo_Value),
    REQUIRED_API(MB),
    REQUIRED_API(PreventUIRefresh),
    REQUIRED_API(SetGlobalAutomationOverride),
    REQUIRED_API(SetProjExtState),
    REQUIRED_API(ShowConsoleMsg),
    REQUIRED_API(TrackFX_GetCount),
    REQUIRED_API(TrackFX_GetEnabled),
    REQUIRED_API(TrackFX_GetFXGUID),
    REQUIRED_API(TrackFX_GetFXName),
    REQUIRED_API(TrackFX_GetNamedConfigParm),
    REQUIRED_API(TrackFX_GetRecCount),
    REQUIRED_API(TrackFX_SetEnabled),
    REQUIRED_API(TrackFX_SetNamedConfigParm),
    REQUIRED_API(TrackFX_SetParam),
    REQUIRED_API(TrackFX_SetParamNormalized),
    REQUIRED_API(Undo_BeginBlock),
    REQUIRED_API(Undo_EndBlock),
    REQUIRED_API(ValidatePtr),
    REQUIRED_API(ValidatePtr2),
    REQUIRED_API(guidToString),
    REQUIRED_API(plugin_register),
    REQUIRED_API(time_precise)
  };

  for (const ApiFunc& func : funcs)
  {
    *func.ptr = getFunc(func.name);

    if (func.required && !*func.ptr)
    {
      fprintf(
        stderr, "[ReaLlm] Unable to import the following API function: %s\n",
        func.name
      );
      return false;
    }
  }

  return true;
}

extern "C"
{
REAPER_PLUGIN_DLL_EXPORT auto REAPER_PLUGIN_ENTRYPOINT(
  REAPER_PLUGIN_HINSTANCE hInstance, reaper_plugin_info_t* rec
) -> int
{
  (void)hInstance;
  if (rec != nullptr && loadAPI(rec->GetFunc)) // (rec->GetFunc) == 0)
  {
    if (rec->GetFunc("Llm_Do"))
    {
      return 0;
    }

    auto version = std::stod(GetAppVersion());
    if (version < 5.75)
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