
#include "Llm.hpp"
#include <cstring>
#include <math.h>
#include <reaper_plugin_functions.h>
#include <reascript_vararg.hpp>
#include <regex>
#include <sstream>

#define BUFSIZCHUNK 512
#define BUFSIZGUID 64
#define BUFSIZNEEDBIG 32768
#define BUFSIZSMALL 8

using namespace std;

namespace llm {

Llm::Llm()
    : pdcModeCheck {false}
    , fxGuidMap {}
    , fxMap {}
    , fxDisabled {}
    , fxSafe {}
    , fxToDisable {}
    , masterTrack {}
    , network {}
    , routes {}
    , inputTracks {}
    , stack {}
    , pdcMax {0}
    , reaperVersion {0}
    , bsize {0}
    , globalAutomationOverride {}
    , pdcLimit {0}
    , projectStateChangeCount {}
{
}

Llm* Llm::instance {new Llm()};
int Llm::commandId {-1};
int Llm::state {0};

void Llm::UpdateNetwork(MediaTrack* tr)
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
    char buf[BUFSIZSMALL];
    if (GetAudioDeviceInfo("BSIZE", buf, BUFSIZSMALL)) {
        bsize = stoi(buf);
    }
    if (pdcLimit == 0) {
        pdcLimit = bsize;
    }
    for (auto i = 0; i < GetNumTracks() + 1; i++) {
        auto node = GetTrack(0, i);
        if (!node) {
            node = masterTrack;
        }
        if (ValidatePtr(tr, "MediaTrack*") && tr != node) {
            continue;
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

        for (auto j = 0; j < TrackFX_GetCount(node); j++) {
            GUID* g = TrackFX_GetFXGUID(node, j);
            if (fxMap.find(g) == fxMap.end()) {
                char bufGuid[BUFSIZGUID];
                guidToString(g, bufGuid);
                fxGuidMap.emplace(pair(string(bufGuid), g));
            }
            fxMap.emplace(pair(g, TrackFX(node, i, g, j)));
        }

        neighbor = node;
        for (auto j = 0; j < GetTrackNumSends(node, -1); j++) {
            auto mute = (bool)GetTrackSendInfo_Value(neighbor, -1, j, "B_MUTE");
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
        char buf[BUFSIZGUID];
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

        string k;
        stringstream ss;

        auto bufSz = BUFSIZNEEDBIG;
        char* p = nullptr;
        if (!p) {
            while (!(p = (char*)realloc(p, sizeof(char) * bufSz)))
                ;
        }

        (void)GetProjExtState(0, extName, keySafe, p, (int)bufSz);
        while (strlen(p) + 1 == bufSz) {
            bufSz = bufSz * 2;
            while (!(p = (char*)realloc(p, sizeof(char) * bufSz)))
                ;
            (void)GetProjExtState(0, extName, keySafe, p, (int)bufSz);
        }

        // auto k = strtok(p, " ");
        // while (k) {
        //     auto v = fxGuidMap.find(k);
        //     if (v != fxGuidMap.end()) {
        //         fxSafe.push_back(v->second);
        //     }
        //     k = strtok(nullptr, " ");
        // }
        ss.str(p);
        while (ss >> k) {
            auto v = fxGuidMap.find(k);
            if (v != fxGuidMap.end()) {
                fxSafe.push_back(v->second);
            }
        }

        (void)GetProjExtState(0, extName, key, p, (int)bufSz);
        while (strlen(p) + 1 == bufSz) {
            bufSz = bufSz * 2;
            while (!(p = (char*)realloc(p, sizeof(char) * bufSz)))
                ;
            (void)GetProjExtState(0, extName, key, p, (int)bufSz);
        }

        // k = strtok(p, " ");
        // while (k) {
        //     auto v = fxGuidMap.find(k);
        //     if (v != fxGuidMap.end()) {
        //         fxDisabled.push_back(v->second);
        //     }
        //     k = strtok(nullptr, " ");
        // }
        k.clear();
        ss.clear();
        ss.str(p);
        while (ss >> k) {
            auto v = fxGuidMap.find(k);
            if (v != fxGuidMap.end()) {
                fxDisabled.push_back(v->second);
            }
        }
        free(p);
    }
    return;
}

double Llm::GetLatency(MediaTrack* tr, double& pdcCurrent)
{
    char buf[BUFSIZCHUNK];
    auto pdcMode {-1};
    auto pdcTemp {0.};

    if (reaperVersion < 6.20) {
        pdcMode = 0;
    }

    if (pdcModeCheck == true && reaperVersion > 6.19) {
        while (!GetTrackStateChunk(tr, buf, BUFSIZCHUNK, false))
            ;
        const regex re("PDC_OPTIONS (\\d+)");
        cmatch match;
        regex_search(buf, match, re);
        string s = string(match[1]);
        if (s == "0" || s == "2") {
            pdcMode = stoi(s);
        }
    }

    const auto instrument = TrackFX_GetInstrument(tr);

    for (auto i = 0; i < TrackFX_GetCount(tr); i++) {
        char bufPdc[BUFSIZSMALL];
        while (!TrackFX_GetNamedConfigParm(tr, i, "pdc", bufPdc, BUFSIZSMALL))
            ;
        auto pdc = stoi(bufPdc) * 1.;
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
                    pdcCurrent + ceil(pdcTemp / bsize) * bsize > pdcLimit) {
                    pdcTemp = pdcTemp - pdc;
                    auto k = find(fxToDisable.begin(), fxToDisable.end(), guid);
                    if (k == fxToDisable.end()) {
                        fxToDisable.push_back(guid);
                    }
                }
                else if (
                    pdcMode == 0 ||
                    (pdcMode == 2 && pdcCurrent + pdc > pdcLimit)) {
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
    "void\0bool*\0exitInOptional\0"
    "Executes one ReaLlm cycle. "
    "E.g. for running ReaLlm on custom timer, or deferred."
    "Or as 'one shot'. "
    "Optional boolean true performs 'shutdown'. "
    "In fact, this is what toggling ReaLlm action on/off does; "
    "Runs Llm_Do() on default timer, and executes Llm_Do(true) at exit.";
void Llm::Do(bool* exit)
{
    Llm& llm = Llm::GetInstance();
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

    if (llm.bsize == 0 || (exit != nullptr && *exit == true)) {
        llm.fxToDisable.clear();
    }

    auto isSet = llm.ProcessTrackFXs();
    if (isSet) {
        llm.GetSetState(true);
    }

    return;
}

Llm& Llm::GetInstance()
{
    return *instance;
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
    "void\0const char*,char*,int,MediaTrack*\0"
    "parmname,"
    "bufOutNeedBig,"
    "bufOutNeedBig_sz,"
    "trInOptional\0"
    "Get ReaLlm information string. Zero-based indices. Master track index -1. "
    "Optional MediaTrack* tr gets results relative to tr. "
    "Each line (newline '\\n' separated) represents entry. "
    "Tracks are separated with ';'. "
    "FX are listed after ':' separated with ','. "
    "\n" //
    "P_GRAPH: "
    "Mixer routings as network graph in format "
    "\"node;neighborhood\\n\" "
    "where node is track, and neighborhood is group of tracks in format "
    "\"track;tr#1;tr#2...\\n\". "
    "Or as \"parent;children\\n\" where first field is parent and rest are "
    "children. "
    "Or as multiply linked list where first field is node and rest are links. "
    "E.g. \"7;1;-1;\\n\" would mean "
    "\"8th track is connected to 2nd track and Master track.\""
    "\n" //
    "P_PDCLATENCY: "
    "PDC latency in samples."
    "\n" //
    "P_PDCLIMIT: "
    "PDC latency limit in samples."
    "\n" //
    "P_PDCMODECHECK: "
    "Is PDC mode check enabled? \"0\" or \"1\"."
    "\n" //
    "P_REALLM or P_STATE: "
    "Current state of ReaLlm as approach vektors with disabled FX in "
    "format: "
    "\"begin:disabled fx,...;next:fx,...;end:fx,...;\\n.\" "
    "E.g. \"3:1,2;0;-1:0\\n\" would be: "
    "4th track, fx#2 and #3 disabled => 1st track, nofx disabled => "
    "Master track, fx#1 disabled."
    "\n" //
    "P_SAFE: "
    "'Safed' plugins as \"track#:fx#\\n\" pairs."
    "\n" //
    "P_VECTOR: "
    "As ReaLlm without FX information. Faster.";

void Llm::Get(const char* parmname, char* buf, int bufSz, MediaTrack* tr)
{
    Llm& llm = Llm::GetInstance();
    scoped_lock lock(llm.m);

    string s;

    if (strcmp(parmname, "P_REALLM") == 0 || strcmp(parmname, "P_STATE") == 0 ||
        strcmp(parmname, "P_VECTOR") == 0) {
        llm.UpdateNetwork(tr);
        llm.GetSetState();
        auto pdcTemp = llm.pdcModeCheck;
        llm.pdcModeCheck = false;
        if (ValidatePtr(tr, "MediaTrack*")) {
            llm.inputTracks.clear();
            llm.inputTracks.push_back(tr);
        }
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
                s.append(to_string(trNum));
                if (strcmp(parmname, "P_VECTOR") != 0) {
                    for (auto k = 0; k < TrackFX_GetCount(j); k++) {
                        if (k == 0) {
                            s.append(":");
                        }
                        auto g = TrackFX_GetFXGUID(j, k);
                        auto v = find(
                            llm.fxDisabled.begin(),
                            llm.fxDisabled.end(),
                            g);
                        if (v != llm.fxDisabled.end()) {
                            s.append(to_string(k));
                            s.append(",");
                        }
                    }
                }
                if (!s.empty() && s.back() == ',') {
                    s.pop_back();
                }
                s.append(";");
            }
            if (!s.empty() && s.back() == ';') {
                s.pop_back();
            }
            if (!s.empty()) {
                s.append("\n");
            };
        }
    }

    else if (strcmp(parmname, "P_GRAPH") == 0) {
        llm.UpdateNetwork(tr);
        for (auto&& i : llm.network) {
            if (i.second.empty()) {
                continue;
            }
            auto trNum =
                (int)GetMediaTrackInfo_Value(i.first, "IP_TRACKNUMBER") - 1;
            if (trNum < -1) {
                trNum = -1;
            }
            s.append(to_string(trNum));
            s.append(";");
            for (auto&& j : i.second) {
                auto trNumNeighbor =
                    (int)GetMediaTrackInfo_Value(j, "IP_TRACKNUMBER") - 1;
                if (trNumNeighbor < -1) {
                    trNumNeighbor = -1;
                }
                s.append(to_string(trNumNeighbor));
                s.append(";");
            }
            if (!s.empty() && s.back() == ';') {
                s.pop_back();
            }
            if (!s.empty()) {
                s.append("\n");
            };
        }
    }

    else if (strcmp(parmname, "P_SAFE") == 0) {
        llm.UpdateNetwork();
        llm.GetSetState();
        for (auto i = llm.fxSafe.begin(); i != llm.fxSafe.end();) {
            auto v = llm.fxMap.find(*i);
            if (v == llm.fxMap.end()) {
                i = llm.fxSafe.erase(i);
            }
            else {
                ++i;
            }
        }
        for (auto&& i : llm.fxSafe) {
            s.append(to_string(llm.fxMap.at(i).trIdx));
            s.append(":");
            s.append(to_string(llm.fxMap.at(i).fxIdx));
            s.append("\n");
        }
    }

    else if (strcmp(parmname, "P_PDCLATENCY") == 0) {
        s.append(to_string((int)(llm.pdcMax + 0.5)));
    }
    else if (strcmp(parmname, "P_PDCLIMIT") == 0) {
        s.append(to_string(llm.pdcLimit));
    }
    else if (strcmp(parmname, "P_PDCMODECHECK") == 0) {
        if (llm.pdcModeCheck == true) {
            s.append("1");
        }
        else {
            s.append("0");
        }
    }

    auto n = (int)s.size();
    if (realloc_cmd_ptr(&buf, &bufSz, n)) {
        strncpy(buf, s.c_str(), (size_t)bufSz);
    }
    else {
        if (n >= bufSz) {
            n = bufSz - 1;
        }
        strncpy(buf, s.c_str(), (size_t)n);
        buf[n + 1] = '\0';
    }

    return;
}

const char* Llm::defstring_Set =
    "void\0const char*,const char*\0"
    "parmname,"
    "bufIn\0"
    "Set ReaLlm parameters."
    "\n" //
    "P_PDCLIMIT: "
    "PDC latency limit in samples. Should be multiple of audio block/buffer "
    "size, in most cases."
    "\n" //
    "P_PDCMODECHECK: "
    "\"0\" or \"1\".";

void Llm::Set(const char* parmname, const char* buf)
{
    Llm& llm = Llm::GetInstance();
    scoped_lock lock(llm.m);

    if (strcmp(parmname, "P_PDCLIMIT") == 0) {
        llm.pdcLimit = stoi(buf);
    }

    else if (strcmp(parmname, "P_PDCMODECHECK") == 0) {
        if (stoi(buf) == 0) {
            llm.pdcModeCheck = false;
        }
        else if (stoi(buf) == 1) {
            llm.pdcModeCheck = true;
        }
    }

    return;
}

void Llm::Register(bool load)
{
    custom_action_register_t action {
        0,
        "AK5K_REALLM",
        "ReaLlm: REAPER Low latency monitoring",
        nullptr};

    if (load) {
        commandId = plugin_register("custom_action", &action);
        plugin_register("hookcommand2", (void*)&CommandHook);
        plugin_register("toggleaction", (void*)&ToggleActionCallback);

        plugin_register("API_Llm_Do", (void*)&Do);
        plugin_register("APIdef_Llm_Do", (void*)defstring_Do);
        plugin_register(
            "APIvararg_Llm_Do",
            reinterpret_cast<void*>(&InvokeReaScriptAPI<&Do>));
        plugin_register("API_Llm_Get", (void*)&Get);
        plugin_register("APIdef_Llm_Get", (void*)defstring_Get);
        plugin_register(
            "APIvararg_Llm_Get",
            reinterpret_cast<void*>(&InvokeReaScriptAPI<&Get>));
        plugin_register("API_Llm_Set", (void*)&Set);
        plugin_register("APIdef_Llm_Set", (void*)defstring_Set);
        plugin_register(
            "APIvararg_Llm_Set",
            reinterpret_cast<void*>(&InvokeReaScriptAPI<&Set>));
    }
    else {
        plugin_register("-timer", (void*)&Do);

        plugin_register("-custom_action", &action);
        plugin_register("-hookcommand2", (void*)&CommandHook);
        plugin_register("-toggleaction", (void*)&ToggleActionCallback);

        plugin_register("-API_Llm_Do", (void*)&Do);
        plugin_register("-APIdef_Llm_Do", (void*)defstring_Do);
        plugin_register(
            "-APIvararg_Llm_Do",
            reinterpret_cast<void*>(&InvokeReaScriptAPI<&Do>));
        plugin_register("-API_Llm_Get", (void*)&Get);
        plugin_register("-APIdef_Llm_Get", (void*)defstring_Get);
        plugin_register(
            "-APIvararg_Llm_Get",
            reinterpret_cast<void*>(&InvokeReaScriptAPI<&Get>));
        plugin_register("-API_Llm_Set", (void*)&Set);
        plugin_register("-APIdef_Llm_Set", (void*)defstring_Set);
        plugin_register(
            "-APIvararg_Llm_Set",
            reinterpret_cast<void*>(&InvokeReaScriptAPI<&Set>));

        delete instance;
    }
    return;
}

} // namespace llm