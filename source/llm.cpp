#include "llm.hpp"
#include "node.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <future>
#include <reaper_plugin_functions.h>
#include <reascript_vararg.hpp>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

using namespace std;

namespace llm {

static bool pdc_mode_check {false};
static double reaper_version {};
static int command_id {};
static int global_automation_override {};
static int pdc_limit {};
static int pdc_max {};
static int project_state_change_count {0};
static int bsize {};
static int state {};
static mutex m {};
static unordered_map<GUID*, TrackFX> fx_map {};
static unordered_map<string, GUID*> fx_guid_map {};
static vector<GUID*>* fx_disabled {};
static vector<GUID*>* fx_to_disable {};
static vector<GUID*>* fx_safe {};
static vector<MediaTrack*>* input_tracks {};

template class Node<MediaTrack*, GUID*, int>;

template <typename T, typename U, typename V>
std::vector<T> Node<T, U, V>::get_neighborhood(T& k)
{
    std::vector<T> v {};
    auto neighbor = GetParentTrack(k);
    auto link = (bool)GetMediaTrackInfo_Value(k, "B_MAINSEND");

    if (neighbor && link) {
        v.push_back(neighbor);
    }

    else if (!neighbor && link && k != GetMasterTrack(0)) {
        v.push_back(GetMasterTrack(0));
    }

    for (auto i = 0; i < GetTrackNumSends(k, 0); i++) {
        auto mute = (bool)GetTrackSendInfo_Value(k, 0, i, "B_MUTE");
        neighbor = (MediaTrack*)(uintptr_t)
            GetTrackSendInfo_Value(k, 0, i, "P_DESTTRACK");
        if (!mute) {
            v.push_back(neighbor);
        }
    }
    return v;
}

template <typename T, typename U, typename V>
V& Node<T, U, V>::analyze(T& k, V& v)
{
    auto& tr = k;
    auto& pdc_current = v;
    auto pdc_mode {-1};
    auto pdc_temp {0};

    if (reaper_version < 6.20) {
        pdc_mode = 0;
    }

    else if (pdc_mode_check == true && reaper_version > 6.19) {
        char buf[BUFSZCHUNK];
        (void)GetTrackStateChunk(k, buf, BUFSZCHUNK, false);
        const std::regex re("PDC_OPTIONS (\\d+)");
        cmatch match;
        regex_search(buf, match, re);
        string s = string(match[1]);
        if (s == "0" || s == "2") {
            pdc_mode = stoi(s);
        }
    }

    auto instrument = TrackFX_GetInstrument(tr);

    for (auto i = 0; i < TrackFX_GetCount(tr); i++) {
        auto pdc {0};
        char buf_pdc[BUFSZSMALL];
        (void)TrackFX_GetNamedConfigParm(tr, i, "pdc", buf_pdc, BUFSZSMALL);
        char buf_name[BUFSZGUID];
        (void)TrackFX_GetFXName(tr, i, buf_name, BUFSZGUID);
        if (string(buf_name).find("ReaInsert") != string::npos) {
            (void)strncpy(buf_pdc, "32768", BUFSZSMALL);
        }
        if (strlen(buf_pdc) == 0) {
            (void)strncpy(buf_pdc, "0", BUFSZSMALL);
        }
        pdc = stoi(buf_pdc);

        auto is_enabled = TrackFX_GetEnabled(tr, i);
        auto guid = TrackFX_GetFXGUID(tr, i);

        auto was_disabled = false;
        if (find(fx_disabled->begin(), fx_disabled->end(), guid) !=
            fx_disabled->end()) {
            was_disabled = true; // previously disabled by llm
        }

        auto safe = false;
        if ((is_enabled && was_disabled) || i == instrument) {
            fx_safe->push_back(guid);
            safe = true;
        }
        else if (
            find(fx_safe->begin(), fx_safe->end(), guid) != fx_safe->end()) {
            safe = true;
        }

        if (!is_enabled) {
            auto k = find(fx_safe->begin(), fx_safe->end(), guid);
            if (k != fx_safe->end()) {
                safe = false;
                fx_safe->erase(k);
                was_disabled = true;
            }
        }

        if (!is_enabled && !was_disabled) {
            pdc = 0;
        }

        if (pdc > 0) {
            if (pdc_mode == 0) {
                pdc = (1 + (pdc / bsize)) * bsize;
            }
            pdc_temp = pdc_temp + pdc;
            if (!safe) {
                if (pdc_mode == -1 &&
                    (pdc_current +
                         (int)(ceil((double)pdc_temp / bsize)) * bsize >
                     pdc_limit)) {
                    pdc_temp = pdc_temp - pdc;
                    auto k = find(results.begin(), results.end(), guid);
                    if (k == results.end()) {
                        results.push_back(guid);
                    }
                }
                else if (
                    (pdc_mode == 0 || pdc_mode == 2) &&
                    pdc_current + pdc_temp > pdc_limit) {
                    pdc_temp = pdc_temp - pdc;
                    auto k = find(results.begin(), results.end(), guid);
                    if (k == results.end()) {
                        results.push_back(guid);
                    }
                }
            }
        }
    }

    if (pdc_mode == -1 && pdc_temp > 0) {
        pdc_current =
            pdc_current + (int)(ceil((double)pdc_temp / bsize)) * bsize;
    }
    else if (pdc_temp > 0) {
        pdc_current = pdc_current + pdc_temp;
    }

    if (pdc_current > pdc_max) {
        pdc_max = pdc_current;
    }

    return pdc_current;
}

static void set_input_tracks(vector<MediaTrack*>& v)
{
    global_automation_override = GetGlobalAutomationOverride();
    for (auto i = 0; i < GetNumTracks() + 1; i++) {
        auto tr = GetTrack(0, i);
        if (!tr) {
            tr = GetMasterTrack(0);
        }

        auto flags {0};
        auto track_automation_mode = GetTrackAutomationMode(tr);
        (void)GetTrackState(tr, &flags);
        if ((flags & 64 && (flags & 128 || flags & 256)) ||
            (global_automation_override > 1 &&
             global_automation_override < 6) ||
            (track_automation_mode > 1 && track_automation_mode < 6)) {
            v.push_back(tr);
        }

        for (auto j = 0; j < TrackFX_GetCount(tr); j++) {
            auto g = TrackFX_GetFXGUID(tr, j);
            auto v = fx_map.find(g);
            if (v != fx_map.end()) {
                char buf[BUFSZGUID];
                guidToString(g, buf);
                fx_guid_map.emplace(string {buf}, g);
            }
            fx_map.insert_or_assign(g, TrackFX {tr, i, g, j});
        }
    }
    return;
}

static bool process_fx(unordered_set<GUID*>& fx_to_disable)
{
    auto res = false;
    vector<GUID*> fx_to_enable {};

    for (auto i = fx_disabled->begin(); i != fx_disabled->end();) {
        auto v = find(fx_to_disable.begin(), fx_to_disable.end(), *i);
        if (v != fx_to_disable.end()) {
            fx_to_disable.erase(v);
            ++i;
        }
        else {
            fx_to_enable.push_back(*i);
            i = fx_disabled->erase(i);
        }
    }

    if (!fx_to_enable.empty() || !fx_to_disable.empty()) {
        auto preventCount = fx_to_disable.size() + fx_to_enable.size() + 4;

        for (auto i = fx_safe->begin(); i != fx_safe->end();) {
            auto v = fx_map.find(*i);
            if (v == fx_map.end()) {
                i = fx_safe->erase(i);
            }
            else {
                ++i;
            }
        }

        PreventUIRefresh((int)preventCount);
        Undo_BeginBlock();
        SetGlobalAutomationOverride(6);

        for (auto&& i : fx_to_enable) {
            auto trackFX = fx_map.at(i);
            if (ValidatePtr2(0, trackFX.tr, "MediaTrack*")) {
                TrackFX_SetEnabled(trackFX.tr, trackFX.fx_idx, true);
            }
        }

        for (auto&& i : fx_to_disable) {
            auto trackFX = fx_map.at(i);
            if (ValidatePtr2(0, trackFX.tr, "MediaTrack*")) {
                TrackFX_SetEnabled(trackFX.tr, trackFX.fx_idx, false);
                fx_disabled->push_back(i);
            }
        }

        SetGlobalAutomationOverride(global_automation_override);
        Undo_EndBlock("ReaLlm: REAPER Low latency monitoring", UNDO_STATE_FX);
        PreventUIRefresh(-1 * (int)preventCount);
        res = true;
    }
    return res;
}

static void GetSetState(bool is_set = false)
{
    constexpr char extName[] = "ak5k";
    constexpr char key[] = "ReaLlm";
    constexpr char keySafe[] = "ReaLlmSafe";

    if (is_set) {
        char buf[BUFSZGUID];
        string s;

        s.clear();
        for (auto&& i : *fx_disabled) {
            guidToString(i, buf);
            s.append(buf);
            s.append(" ");
        }
        (void)SetProjExtState(0, extName, key, s.c_str());

        s.clear();
        for (auto&& i : *fx_safe) {
            guidToString(i, buf);
            s.append(buf);
            s.append(" ");
        }
        (void)SetProjExtState(0, extName, keySafe, s.c_str());
    }
    else {
        string k {};
        stringstream ss {};

        size_t bufSz = BUFSZNEEDBIG;
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

        ss.str(p);
        while (ss >> k) {
            auto v = fx_guid_map.find(k);
            if (v != fx_guid_map.end()) {
                fx_safe->push_back(v->second);
            }
        }

        (void)GetProjExtState(0, extName, key, p, (int)bufSz);
        while (strlen(p) + 1 == bufSz) {
            bufSz = bufSz * 2;
            while (!(p = (char*)realloc(p, sizeof(char) * bufSz)))
                ;
            (void)GetProjExtState(0, extName, key, p, (int)bufSz);
        }

        k.clear();
        ss.clear();
        ss.str(p);
        while (ss >> k) {
            auto v = fx_guid_map.find(k);
            if (v != fx_guid_map.end()) {
                fx_disabled->push_back(v->second);
            }
        }
        free(p);
    }
    return;
}

const char* defstring_Do =
    "void\0bool*\0exitInOptional\0"
    "Executes one ReaLlm cycle. "
    "E.g. for running ReaLlm on custom timer, or deferred. "
    "Or as 'one shot'. PDC mode check works only with multiple successive "
    "Llm_Do() calls. "
    "Optional boolean true performs 'shutdown'. "
    "In fact, this is what toggling ReaLlm: REAPER Low latency monitoring "
    "action on/off does; "
    "Runs Llm_Do() on default timer, and executes Llm_Do(true) at exit.";
static void Do(bool* exit)
{
    // auto time0 = time_precise();
    scoped_lock lock(m);
    reaper_version = stod(GetAppVersion());
    char buf[BUFSZSMALL];
    if (GetAudioDeviceInfo("BSIZE", buf, BUFSZSMALL)) {
        auto temp = stoi(buf);
        if (temp != bsize) {
            bsize = temp;
            pdc_limit = bsize;
        }
    }

    vector<MediaTrack*> v4 {};
    input_tracks = &v4;
    input_tracks->reserve(8);

    set_input_tracks(*input_tracks);

    auto project_state_change_count_now =
        GetProjectStateChangeCount(0) + global_automation_override;
    if (project_state_change_count_now != project_state_change_count) {
        project_state_change_count = project_state_change_count_now;
    }
    else {
        return;
    }

    vector<GUID*> v1 {}, v2 {}, v3 {};
    fx_disabled = &v1;
    fx_to_disable = &v2;
    fx_safe = &v3;

    pdc_max = 0;
    GetSetState();
    if (exit != nullptr && *exit == true) {
        input_tracks->clear();
    }
    // vector<future<vector<GUID*>>> v {};
    for (auto&& i : *input_tracks) {
        Node<MediaTrack*, GUID*, int> n {i};
        auto& results = n.traverse(true);
        fx_to_disable->insert(
            fx_to_disable->end(),
            results.begin(),
            results.end());
        // v.emplace_back(async([i] {
        //     Node<MediaTrack*, GUID*, int> n {i};
        //     return n.traverse(true);
        // }));
    }
    // for (auto&& i : v) {
    //     auto results = i.get();
    //     fx_to_disable->insert(
    //         fx_to_disable->end(),
    //         results.begin(),
    //         results.end());
    // }
    unordered_set<GUID*> fx_to_disable_unique(
        fx_to_disable->begin(),
        fx_to_disable->end());

    if (process_fx(fx_to_disable_unique)) {
        GetSetState(true);
    }

    // auto currentProjectStateChangeCount =
    //     GetProjectStateChangeCount(0) + llm.globalAutomationOverride;

    // if (currentProjectStateChangeCount != llm.projectStateChangeCount ||
    //     (exit != nullptr && *exit == true)) {

    // llm.projectStateChangeCount = currentProjectStateChangeCount;

    if (exit != nullptr && *exit == true) {
        fx_map.clear();
        fx_guid_map.clear();
    }

    // auto time1 = time_precise() - time0;
    // ShowConsoleMsg((to_string(time1) + string("\n")).c_str());

    return;
}

static bool CommandHook(
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

    if (command == command_id) {
        {
            scoped_lock lock(m);
            state = !state;
        }
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

static int ToggleActionCallback(int command)
{
    if (command != command_id) {
        return -1;
    }
    else {
        scoped_lock lock(m);
        return state;
    }
}

const char* defstring_Get =
    "void\0const char*,char*,int,MediaTrack*\0"
    "parmname,"
    "bufOutNeedBig,"
    "bufOutNeedBig_sz,"
    "trInOptional\0"
    "Get ReaLlm information string. Zero-based indices. Master track index "
    "-1. "
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
    "Or as multiply linked list where first field is node and rest are "
    "links. "
    "E.g. \"7;1;-1;\\n\" would mean "
    "\"8th track is connected to 2nd track and Master track.\""
    "\n" //
    "P_PDCLATENCY: "
    "Latency in samples."
    "\n" //
    "P_PDCLIMIT: "
    "Limit in samples."
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
    "P_VECTOR: Same as P_REALLM without FX information. Faster.";

static void Get(
    const char* parmname,
    char* buf = nullptr,
    int bufSz = 0,
    MediaTrack* tr = nullptr)
{
    scoped_lock lock(m);
    string s {};
    vector<MediaTrack*> v {};
    input_tracks = &v;
    GetSetState();

    if (strcmp(parmname, "P_REALLM") == 0 || strcmp(parmname, "P_STATE") == 0 ||
        strcmp(parmname, "P_VECTOR") == 0) {
        // llm.UpdateNetwork(tr);
        auto pdc_mode_check_temp = pdc_mode_check;
        pdc_mode_check = false;
        if (ValidatePtr2(0, tr, "MediaTrack*")) {
            input_tracks->push_back(tr);
        }
        else {
            set_input_tracks(*input_tracks);
        }
        vector<vector<MediaTrack*>> routes {};
        for (auto&& i : *input_tracks) {
            Node<MediaTrack*, GUID*, int> n {i};
            n.traverse(false);
            for (auto&& i : n.get_routes()) {
                routes.push_back(i);
            }
        }
        pdc_mode_check = pdc_mode_check_temp;
        for (auto&& i : routes) {
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
                        auto v =
                            find(fx_disabled->begin(), fx_disabled->end(), g);
                        if (v != fx_disabled->end()) {
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
        unordered_map<MediaTrack*, vector<MediaTrack*>> network;
        for (auto i = 0; i < GetNumTracks(); i++) {
            // Node<MediaTrack*, GUID*, int> n {GetTrack(0, i)};
            // network.emplace(pair {n.get(), n.get_neighborhood(n.get())});
        }
        for (auto&& i : network) {
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
        set_input_tracks(*input_tracks);
        for (auto i = fx_safe->begin(); i != fx_safe->end();) {
            auto v = fx_map.find(*i);
            if (v == fx_map.end()) {
                i = fx_safe->erase(i);
            }
            else {
                ++i;
            }
        }
        for (auto&& i : *fx_safe) {
            s.append(to_string(fx_map.at(i).tr_idx));
            s.append(":");
            s.append(to_string(fx_map.at(i).fx_idx));
            s.append("\n");
        }
    }

    else if (strcmp(parmname, "P_PDCLATENCY") == 0) {
        s.append(to_string((int)(pdc_max + 0.5)));
    }
    else if (strcmp(parmname, "P_PDCLIMIT") == 0) {
        s.append(to_string(pdc_limit));
    }
    else if (strcmp(parmname, "P_PDCMODECHECK") == 0) {
        if (pdc_mode_check == true) {
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
        if (n > bufSz - 1) {
            n = bufSz - 1;
        }
        strncpy(buf, s.c_str(), (size_t)n);
        buf[n + 1] = '\0';
    }

    return;
}

void Register(bool load)
{
    custom_action_register_t action {
        0,
        "AK5K_REALLM",
        "ReaLlm: REAPER Low latency monitoring",
        nullptr};

    if (load) {
        command_id = plugin_register("custom_action", &action);
        plugin_register("hookcommand2", (void*)&CommandHook);
        plugin_register("toggleaction", (void*)&ToggleActionCallback);

        plugin_register("API_Llm_Do", (void*)&Do);
        // plugin_register("APIdef_Llm_Do", (void*)defstring_Do);
        // plugin_register(
        //     "APIvararg_Llm_Do",
        //     reinterpret_cast<void*>(&InvokeReaScriptAPI<&Do>));
        // plugin_register("API_Llm_Get", (void*)&Get);
        // plugin_register("APIdef_Llm_Get", (void*)defstring_Get);
        // plugin_register(
        //     "APIvararg_Llm_Get",
        //     reinterpret_cast<void*>(&InvokeReaScriptAPI<&Get>));
        // plugin_register("API_Llm_Set", (void*)&Set);
        // plugin_register("APIdef_Llm_Set", (void*)defstring_Set);
        // plugin_register(
        //     "APIvararg_Llm_Set",
        //     reinterpret_cast<void*>(&InvokeReaScriptAPI<&Set>));
    }
    else {
        state = 0;

        plugin_register("-timer", (void*)&Do);

        plugin_register("-custom_action", &action);
        // plugin_register("-hookcommand2", (void*)&CommandHook);
        // plugin_register("-toggleaction", (void*)&ToggleActionCallback);

        // plugin_register("-API_Llm_Do", (void*)&Do);
        // plugin_register("-APIdef_Llm_Do", (void*)defstring_Do);
        // plugin_register(
        //     "-APIvararg_Llm_Do",
        //     reinterpret_cast<void*>(&InvokeReaScriptAPI<&Do>));
        // plugin_register("-API_Llm_Get", (void*)&Get);
        // plugin_register("-APIdef_Llm_Get", (void*)defstring_Get);
        // plugin_register(
        //     "-APIvararg_Llm_Get",
        //     reinterpret_cast<void*>(&InvokeReaScriptAPI<&Get>));
        // plugin_register("-API_Llm_Set", (void*)&Set);
        // plugin_register("-APIdef_Llm_Set", (void*)defstring_Set);
        // plugin_register(
        //     "-APIvararg_Llm_Set",
        //     reinterpret_cast<void*>(&InvokeReaScriptAPI<&Set>));

        // delete instance;
    }
    return;
}

} // namespace llm