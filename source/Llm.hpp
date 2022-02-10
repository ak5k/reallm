#pragma once
#include "reaper_api.h"
#include <atomic>
#include <mutex>
#include <reaper_plugin_functions.h>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std;

namespace llm {

class Llm {
    typedef vector<GUID*> GUIDVector;
    typedef vector<MediaTrack*> TrackVector;
    typedef vector<TrackVector> Routes;
    typedef unordered_map<MediaTrack*, TrackVector> Network;
    struct TrackFX {
        MediaTrack* tr;
        int trIdx;
        GUID* g;
        int fxIdx;
        TrackFX(MediaTrack* tr, int trIdx, GUID* g, int fxIdx)
            : tr(tr)
            , trIdx(trIdx)
            , g(g)
            , fxIdx(fxIdx)
        {
        }
    };
    typedef unordered_map<GUID*, TrackFX> FXMap;
    typedef unordered_map<string, GUID*> FXGUIDMap;

  private:
    Llm(); // singleton

    FXGUIDMap fxGuidMap;
    FXMap fxMap;
    GUIDVector fxDisabled;
    GUIDVector fxSafe;
    GUIDVector fxToDisable;
    MediaTrack* masterTrack;
    Network network;
    Routes routes;
    TrackVector inputTracks;
    TrackVector stack;
    double pdcMax;
    double reaperVersion;
    int bsize;
    int globalAutomationOverride;
    int limit;
    int projectStateChangeCount;
    mutex m;

    static atomic<bool> pdcModeCheck;
    static atomic<int> state;
    static int commandId;

    bool ProcessTrackFXs();
    double GetLatency(MediaTrack* tr, double& pdcCurrent);
    void GetSetState(bool isSet = false);
    void TraverseNetwork(
        MediaTrack* node,
        const bool checkLatency = true,
        double pdcCurrent = 0);

    void UpdateNetwork(bool setFxGuidMap = true, MediaTrack* tr = nullptr);

  public:
    // singleton
    Llm(Llm const&) = delete;
    Llm(Llm&&) = delete;
    Llm& operator=(Llm const&) = delete;
    Llm& operator=(Llm&&) = delete;
    static Llm& getInstance();

    static bool CommandHook(
        KbdSectionInfo* sec,
        const int command,
        const int val,
        const int valhw,
        const int relmode,
        HWND hwnd);

    static int ToggleActionCallback(int command);

    static const char* defstring_Do;
    static void Do(bool* exit = nullptr);

    static const char* defstring_Get;
    static void Get(
        const char* parmname,
        char* buf = nullptr,
        int bufSz = 0,
        MediaTrack* tr = nullptr);

    static const char* defstring_Set;
    static void Set(const char* parmname, const char* buf = nullptr);

    static void Register(bool load);
};

} // namespace llm
