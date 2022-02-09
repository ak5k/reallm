#include "Llm.hpp"
#include <cstring>
#include <math.h>
#include <reaper_plugin_functions.h>
#include <reascript_vararg.hpp>
#include <regex>
#include <sstream>

#define CHUNKSIZE 512
#define GUIDSIZE 64
#define SMALLBUFSIZE 8

using namespace std;

namespace llm {

Llm::Llm()
    : fxGuidMap {}
    , fxMap {}
    , fxDisabled {}
    , fxSafe {}
    , fxToDisable {}
    , masterTrack(GetMasterTrack(0))
    , network {}
    , routes {}
    , inputTracks {}
    , stack {}
    , pdcMax {0}
    , reaperVersion {0}
    , bsize {0}
    , globalAutomationOverride(GetGlobalAutomationOverride())
    , limit {0}
    , projectStateChangeCount(
          GetProjectStateChangeCount(0) + globalAutomationOverride)
{
}

atomic<int> Llm::state {0};
atomic<bool> Llm::pdcModeCheck {false};
int Llm::commandId = -1;

void Llm::UpdateNetwork(bool setFxGuidMap)
{
    inputTracks.clear();
    for (auto&& i : network) {
        i.second.clear();
    }

    stack.clear();
    routes.clear();

    globalAutomationOverride = GetGlobalAutomationOverride();
    masterTrack = GetMasterTrack(0);

    reaperVersion = stod(GetAppVersion());
    char buf[SMALLBUFSIZE];
    if (GetAudioDeviceInfo("BSIZE", buf, SMALLBUFSIZE)) {
        bsize = stoi(buf);
    }
    limit = bsize;

    for (auto i = 0; i < GetNumTracks() + 1; i++) {
        auto node = GetTrack(0, i);
        if (!node) {
            node = masterTrack;
        }
        if (network.find(node) == network.end()) {
            network[node] = TrackVector {};
        }
        auto neighbor = GetParentTrack(node);
        auto link = (bool)GetMediaTrackInfo_Value(node, "B_MAINSEND");

        if (neighbor && link) {
            network[node].push_back(neighbor);
        }
        else if (!neighbor && link && node != masterTrack) {
            network[node].push_back(masterTrack);
        }

        auto flags {0};
        auto trackAutomationMode = GetTrackAutomationMode(node);
        (void)GetTrackState(node, &flags);
        if ((flags & 64 && (flags & 128 || flags & 256)) ||
            (globalAutomationOverride > 1 && globalAutomationOverride < 6) ||
            (trackAutomationMode > 1 && trackAutomationMode < 6)) {
            inputTracks.push_back(node);
        }

        if (setFxGuidMap) {
            for (auto j = 0; j < TrackFX_GetCount(node); j++) {
                GUID* g = TrackFX_GetFXGUID(node, j);
                fxMap.emplace(pair(g, TrackFX(node, i, g, j)));
                char buf[GUIDSIZE];
                guidToString(g, buf);
                fxGuidMap.emplace(pair(string(buf), g));
            }
        }

        neighbor = node;
        for (auto j = 0; j < GetTrackNumSends(node, -1); j++) {
            auto mute =
                (bool)GetTrackSendInfo_Value(neighbor, -1, j, "B_MAINSEND");
            node = (MediaTrack*)(uintptr_t)
                GetTrackSendInfo_Value(neighbor, -1, j, "P_SRCTRACK");
            if (!mute) {
                if (network.find(node) == network.end()) {
                    network[node] = TrackVector {};
                }
                network[node].push_back(neighbor);
            }
        }
    }

    return;
}

void Llm::GetSetState(bool isSet)
{
    constexpr char extName[] = "ak5k";
    constexpr char key[] = "ReaLlm";
    constexpr char keySafe[] = "ReaLlmSafe";

    fxToDisable.clear();

    if (isSet) {
        char buf[GUIDSIZE];
        static string s;
        s.clear();
        for (auto&& i : fxDisabled) {
            guidToString(i, buf);
            s.append(buf);
            s.append(" ");
        }
        (void)SetProjExtState(0, extName, key, s.c_str());
        s.clear();
        for (auto&& i : fxSafe) {
            guidToString(i, buf);
            s.append(buf);
            s.append(" ");
        }
        (void)SetProjExtState(0, extName, keySafe, s.c_str());
    }
    else {
        fxDisabled.clear();
        fxSafe.clear();

        static auto bufSz = BUFSIZ;
        static char* p;
        if (!p) {
            while (!(p = (char*)realloc(p, sizeof(char) * bufSz)))
                ;
        }

        (void)GetProjExtState(0, extName, keySafe, p, bufSz);
        while ((int)strlen(p) + 1 == bufSz) {
            bufSz = bufSz * 2;
            while (!(p = (char*)realloc(p, sizeof(char) * bufSz)))
                ;
            (void)GetProjExtState(0, extName, keySafe, p, bufSz);
        }

        auto k = strtok(p, " ");
        while (k) {
            auto v = fxGuidMap.find(k);
            if (v != fxGuidMap.end()) {
                fxSafe.push_back(v->second);
            }
            k = strtok(nullptr, " ");
        }

        (void)GetProjExtState(0, extName, key, p, bufSz);
        while ((int)strlen(p) + 1 == bufSz) {
            bufSz = bufSz * 2;
            while (!(p = (char*)realloc(p, sizeof(char) * bufSz)))
                ;
            (void)GetProjExtState(0, extName, key, p, bufSz);
        }

        k = strtok(p, " ");
        while (k) {
            auto v = fxGuidMap.find(k);
            if (v != fxGuidMap.end()) {
                fxDisabled.push_back(v->second);
            }
            k = strtok(nullptr, " ");
        }
    }
    return;
}

double Llm::GetLatency(MediaTrack* tr, double& pdcCurrent)
{
    char buf[CHUNKSIZE];
    auto pdcMode {-1};
    auto pdcTemp {0.};

    if (reaperVersion < 6.20) {
        pdcMode = 0;
    }

    if (pdcModeCheck.load() && reaperVersion > 6.19) {
        while (!GetTrackStateChunk(tr, buf, CHUNKSIZE, false))
            ;
        const regex re("PDC_OPTIONS (\\d+)");
        cmatch m;
        regex_search(buf, m, re);
        string s = string(m[1]);
        if (s == "0" || s == "2") {
            pdcMode = stoi(s);
        }
    }

    const auto instrument = TrackFX_GetInstrument(tr);

    for (auto i = 0; i < TrackFX_GetCount(tr); i++) {
        char buf[SMALLBUFSIZE];
        while (!TrackFX_GetNamedConfigParm(tr, i, "pdc", buf, SMALLBUFSIZE))
            ;
        auto pdc = stoi(buf) * 1.;
        const auto isEnabled = TrackFX_GetEnabled(tr, i);
        const auto guid = TrackFX_GetFXGUID(tr, i);

        auto wasDisabled = false;
        if (find(fxDisabled.begin(), fxDisabled.end(), guid) !=
            fxDisabled.end()) {
            wasDisabled = true; // previously disabled by llm
        }

        auto safe = false;
        if ((isEnabled && wasDisabled) || i == instrument) {
            fxSafe.push_back(guid);
            safe = true;
        }
        else if (find(fxSafe.begin(), fxSafe.end(), guid) != fxSafe.end()) {
            safe = true;
        }

        if (!isEnabled) {
            auto k = find(fxSafe.begin(), fxSafe.end(), guid);
            if (k != fxSafe.end()) {
                safe = false;
                fxSafe.erase(k);
                wasDisabled = true;
            }
        }

        if (!isEnabled && !wasDisabled) {
            pdc = 0;
        }

        if (pdc > 0) {
            if (pdcMode == 2) {
                pdc = ceil(pdc / bsize) * bsize;
            }
            pdcTemp = pdcTemp + pdc;
            if (!safe) {
                if (pdcMode == -1 &&
                    pdcCurrent + ceil(pdcTemp / bsize) * bsize > limit) {
                    pdcTemp = pdcTemp - pdc;
                    auto k = find(fxToDisable.begin(), fxToDisable.end(), guid);
                    if (k == fxToDisable.end()) {
                        fxToDisable.push_back(guid);
                    }
                }
                else if (
                    pdcMode == 0 ||
                    (pdcMode == 2 && pdcCurrent + pdc > limit)) {
                    pdcTemp = pdcTemp - pdc;
                    auto k = find(fxToDisable.begin(), fxToDisable.end(), guid);
                    if (k == fxToDisable.end()) {
                        fxToDisable.push_back(guid);
                    }
                }
            }
        }
    }

    if (pdcMode == -1 && pdcTemp > 0) {
        pdcCurrent = pdcCurrent + ceil(pdcTemp / bsize) * bsize;
    }
    else if (pdcTemp > 0) {
        pdcCurrent = pdcCurrent + pdcTemp;
    }

    if (pdcCurrent > pdcMax) {
        pdcMax = pdcCurrent;
    }

    return pdcCurrent;
}

void Llm::TraverseNetwork(
    MediaTrack* node,
    bool checkLatency,
    double pdcCurrent)
{
    if (checkLatency) {
        pdcCurrent = GetLatency(node, pdcCurrent);
    }
    const auto& neighborhood = network.at(node);
    if (neighborhood.empty()) {
        stack.push_back(node);
        routes.emplace_back(TrackVector {stack});
        stack.pop_back();
        return;
    }
    else {
        for (auto&& i : neighborhood) {
            if (find(stack.begin(), stack.end(), i) == stack.end()) {
                stack.push_back(node);
                TraverseNetwork(i, checkLatency, pdcCurrent);
                stack.pop_back();
            }
        }
        return;
    }
}

bool Llm::ProcessTrackFXs()
{
    auto res = false;
    GUIDVector fxToEnable {};

    for (auto i = fxDisabled.begin(); i != fxDisabled.end();) {
        auto v = find(fxToDisable.begin(), fxToDisable.end(), *i);
        if (v != fxToDisable.end()) {
            fxToDisable.erase(v);
            ++i;
        }
        else {
            fxToEnable.push_back(*i);
            i = fxDisabled.erase(i);
        }
    }

    if (!fxToEnable.empty() || !fxToDisable.empty()) {
        auto preventCount =
            (int)fxToDisable.size() + (int)fxToEnable.size() + 4;

        for (auto i = fxSafe.begin(); i != fxSafe.end();) {
            auto v = fxMap.find(*i);
            if (v == fxMap.end()) {
                i = fxSafe.erase(i);
            }
            else {
                ++i;
            }
        }

        PreventUIRefresh(preventCount);
        Undo_BeginBlock();
        SetGlobalAutomationOverride(6);

        for (auto&& i : fxToEnable) {
            const TrackFX& trackFX = fxMap.at(i);
            TrackFX_SetEnabled(trackFX.tr, trackFX.fxIdx, true);
        }

        for (auto&& i : fxToDisable) {
            const TrackFX& trackFX = fxMap.at(i);
            TrackFX_SetEnabled(trackFX.tr, trackFX.fxIdx, false);
            fxDisabled.push_back(i);
        }

        SetGlobalAutomationOverride(globalAutomationOverride);
        PreventUIRefresh(-preventCount);
        Undo_EndBlock("Low latency monitoring", -1);
        res = true;
    }
    return res;
}

const char* Llm::defstring_Do =
    "void\0bool*\0exitOptional\0"
    "Executes one ReaLlm cycle.\n"
    "Optional boolean exitIn parameter true performs 'shutdown'.\n";
void Llm::Do(bool* exitInOptional)
{
    auto time0 = time_precise();
    Llm& llm = Llm::getInstance();
    scoped_lock lock(llm.m);

    llm.UpdateNetwork();
    llm.GetSetState(false);

    // auto currentProjectStateChangeCount =
    //     GetProjectStateChangeCount(0) + llm.globalAutomationOverride;

    // if (currentProjectStateChangeCount != llm.projectStateChangeCount) {
    //     llm.projectStateChangeCount = currentProjectStateChangeCount;

    llm.pdcMax = 0;
    for (auto&& i : llm.inputTracks) {
        llm.TraverseNetwork(i);
    }
    // }

    if (llm.bsize == 0 ||
        (exitInOptional != nullptr && *exitInOptional == true)) {
        llm.fxToDisable.clear();
    }

    auto isSet = llm.ProcessTrackFXs();
    if (isSet) {
        llm.GetSetState(true);
    }

    auto time1 = time_precise() - time0;
    ShowConsoleMsg((to_string(time1) + string("\n")).c_str());
    return;
}

Llm& Llm::getInstance()
{
    static Llm* llm = new Llm();
    return *llm;
}

bool Llm::CommandHook(
    KbdSectionInfo* sec,
    const int command,
    const int val,
    const int valhw,
    const int relmode,
    HWND hwnd)
{
    (void)sec;
    (void)val;
    (void)valhw;
    (void)relmode;
    (void)hwnd;

    if (command == commandId) {
        state = !state;
        if (state == 1) {
            plugin_register("timer", (void*)&Do);
        }
        else {
            plugin_register("-timer", (void*)&Do);
            auto exit = true;
            Do(&exit); // exitInOptional is true
        }
        return true;
    }

    return false;
}

int Llm::ToggleActionCallback(int command)
{
    if (command != commandId) {
        return -1;
    }
    else {
        return state;
    }
}

const char* Llm::defstring_Get =
    "void\0const "
    "char*,char*,int,MediaTrack*\0parmname,strOutNeedBig,strOutNeedBig_sz,"
    "trInOptional\0"
    "Get ReaLlm information. Zero-based indices. Master track index -1."
    "\n"
    "P_GRAPH: "
    "Current mixer configuration as graph of network nodes in format "
    "\"node:neighborhood\\n\" "
    "where node is track, and neighborhood is group of tracks in format "
    "\"tr#1;tr#2;...;\". "
    "Or as \"parent:children\\n\". "
    "E.g. \"2:1;-1;\\n\" would mean "
    "\"3rd track is connected to 2nd track and Master track.\""
    "\n"
    "P_STATE: "
    "Current state of ReaLlm as string of approach vektors in format: "
    "\"begin track:disabled fx,...;next:fx,...;end:fx,...;\\n.\" "
    "E.g. \"3:1,2;0;-1:0\\n\" would be: "
    "4th track, fx#2 and #3 disabled => 1st track, nofx disabled => "
    "Master track, fx#1 disabled."
    "\n";

void Llm::Get(
    const char* parmname,
    char* strOutNeedBig,
    int strOutNeedBig_sz,
    MediaTrack* trInOptional)
{
    Llm& llm = Llm::getInstance();
    scoped_lock lock(llm.m);

    static string res;
    res.clear();

    if (strcmp(parmname, "P_STATE") == 0) {
        llm.UpdateNetwork();
        llm.GetSetState();
        auto pdcTemp = llm.pdcModeCheck.load();
        llm.pdcModeCheck = false;
        for (auto&& i : llm.inputTracks) {
            llm.TraverseNetwork(i,
                                false); // ignore plugin latency checks
        }
        llm.pdcModeCheck = pdcTemp;
        for (auto&& i : llm.routes) {
            for (auto&& j : i) {
                auto trNum =
                    (int)GetMediaTrackInfo_Value(j, "IP_TRACKNUMBER") - 1;
                if (trNum < -1) {
                    trNum = -1;
                }
                res.append(to_string(trNum));
                for (auto k = 0; k < TrackFX_GetCount(j); k++) {
                    if (k == 0) {
                        res.append(":");
                    }
                    auto g = TrackFX_GetFXGUID(j, k);
                    auto v =
                        find(llm.fxDisabled.begin(), llm.fxDisabled.end(), g);
                    if (v != llm.fxDisabled.end()) {
                        res.append(to_string(k));
                        res.append(",");
                    }
                }
                if (res.back() == ',') {
                    res.pop_back();
                }
                res.append(";");
            }
            if (res.back() == ';') {
                res.pop_back();
            }
            res.append("\n");
        }
    }

    if (strcmp(parmname, "P_GRAPH") == 0) {
        llm.UpdateNetwork(false);
        for (auto&& i : llm.network) {
            auto trNum =
                (int)GetMediaTrackInfo_Value(i.first, "IP_TRACKNUMBER") - 1;
            if (trNum < -1) {
                trNum = -1;
            }
            res.append(to_string(trNum));
            for (auto&& j : i.second) {
                if (j == *i.second.begin()) {
                    res.append(":");
                }
                auto trNum =
                    (int)GetMediaTrackInfo_Value(j, "IP_TRACKNUMBER") - 1;
                if (trNum < -1) {
                    trNum = -1;
                }
                res.append(to_string(trNum));
                res.append(";");
            }
            if (res.back() == ';') {
                res.pop_back();
            }
            res.append("\n");
        }
    }

    realloc_cmd_ptr(&strOutNeedBig, &strOutNeedBig_sz, (int)res.size());
    strncpy(strOutNeedBig, res.c_str(), strOutNeedBig_sz);

    return;
}

void Llm::Register(bool load)
{
    custom_action_register_t action {
        0,
        "AK5K_REALLM",
        "ReaLlm: REAPER Low latency monitoring",
        nullptr};

    if (!load) {
        plugin_register("-custom_action", &action);
        plugin_register("-hookcommand2", (void*)&CommandHook);
        plugin_register("-toggleaction", (void*)&ToggleActionCallback);

        plugin_register("-API_Llm_Get", (void*)&Get);
        plugin_register("-APIdef_Llm_Get", (void*)defstring_Get);
        plugin_register(
            "-APIvararg_Llm_Get",
            reinterpret_cast<void*>(&InvokeReaScriptAPI<&Get>));
        plugin_register("-API_Llm_Do", (void*)&Do);
        plugin_register("-APIdef_Llm_Do", (void*)defstring_Do);
        plugin_register(
            "-APIvararg_Llm_Do",
            reinterpret_cast<void*>(&InvokeReaScriptAPI<&Do>));
    }
    else {
        commandId = plugin_register("custom_action", &action);
        plugin_register("hookcommand2", (void*)&CommandHook);
        plugin_register("toggleaction", (void*)&ToggleActionCallback);

        plugin_register("API_Llm_Get", (void*)&Get);
        plugin_register("APIdef_Llm_Get", (void*)defstring_Get);
        plugin_register(
            "APIvararg_Llm_Get",
            reinterpret_cast<void*>(&InvokeReaScriptAPI<&Get>));
        plugin_register("API_Llm_Do", (void*)&Do);
        plugin_register("APIdef_Llm_Do", (void*)defstring_Do);
        plugin_register(
            "APIvararg_Llm_Do",
            reinterpret_cast<void*>(&InvokeReaScriptAPI<&Do>));
    }
    return;
}

} // namespace llm