#include "llm.hpp"
#include "node.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <future>
#include <reaper_plugin_functions.h>
#include <reascript_vararg.hpp>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
// #include <unordered_set>

// isolate llm into its own safe space
namespace llm {
// 'import' stl stuff
using namespace std;

// globals
static bool pdc_mode_check {false};
static double reaper_version {};
static int command_id {};
static int global_automation_override {};
static int pdc_limit {};
static int pdc_max {};
static int project_state_change_count {0};
static int bsize {};
static int llm_state {};
unordered_map<string, GUID*> guid_string_map {};

static vector<GUID*>* fx_disabled_g {};

// master lock for thread safety
static mutex m {};

// help IDE to know types while deving
template class Network<MediaTrack*, FXResults, int>;

// initialize FXBase static member
unordered_map<GUID*, FXBase> FXBase::fx_map {};

// assign alias to it
unordered_map<GUID*, FXBase>& fx_map {FXBase::fx_map};

template <typename T, typename U, typename V>
std::vector<T> Network<T, U, V>::get_neighborhood(T& k)
{
    std::vector<T> v {};
    v.reserve(VECTORSIZE);
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
V& Network<T, U, V>::analyze(T& k, U& r, V& v)
{
    auto& tr = k;
    auto& fx_to_disable = r.to_disable;
    auto& fx_safe = r.safe;
    auto& fx_unsafe = r.unsafe;
    auto& pdc_current = v;
    auto pdc_mode {-1};
    auto pdc_temp {0};

    if (reaper_version < 6.20) {
        pdc_mode = 0;
    }

    else if (pdc_mode_check == true && reaper_version > 6.19) {
        char buf[BUFSZCHUNK];
        (void)GetTrackStateChunk(tr, buf, BUFSZCHUNK, false);
        const std::regex re("PDC_OPTIONS (\\d+)");
        cmatch match;
        regex_search(buf, match, re);
        string s = string(match[1]);
        if (s == "0" || s == "2") {
            pdc_mode = stoi(s);
        }
    }

    auto instrument = TrackFX_GetInstrument(tr);

    char buf_pdc[BUFSZSMALL];
    char buf_name[BUFSZGUID];
    for (auto i = 0; i < TrackFX_GetCount(tr); i++) {
        auto pdc {0};
        (void)TrackFX_GetNamedConfigParm(tr, i, "pdc", buf_pdc, BUFSZSMALL);
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
        if (find(fx_disabled_g->cbegin(), fx_disabled_g->cend(), guid) !=
            fx_disabled_g->cend()) {
            was_disabled = true; // previously disabled by llm
        }

        auto safe = false;
        if ((is_enabled && was_disabled) || i == instrument) {
            fx_safe.push_back(guid);
            safe = true;
        }
        else if (
            find(fx_safe.cbegin(), fx_safe.cend(), guid) != fx_safe.cend()) {
            safe = true;
        }

        if (!is_enabled) {
            auto v = find(fx_safe.cbegin(), fx_safe.cend(), guid);
            if (v != fx_safe.cend()) {
                // fx_safe.erase(v);
                fx_unsafe.push_back(*v);
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
                    auto k = find(
                        fx_to_disable.cbegin(),
                        fx_to_disable.cend(),
                        guid);
                    if (k == fx_to_disable.cend()) {
                        fx_to_disable.push_back(guid);
                    }
                }
                else if (
                    (pdc_mode == 0 || pdc_mode == 2) &&
                    pdc_current + pdc_temp > pdc_limit) {
                    pdc_temp = pdc_temp - pdc;
                    auto k = find(
                        fx_to_disable.cbegin(),
                        fx_to_disable.cend(),
                        guid);
                    if (k == fx_to_disable.cend()) {
                        fx_to_disable.push_back(guid);
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

static void initialize(vector<MediaTrack*>& v)
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
            FXBase {tr, j};
        }
    }
    return;
}

static bool process_fx(set<GUID*>& fx_to_disable, vector<GUID*>& fx_safe)
{
    auto res = false;
    vector<GUID*> fx_to_enable {};
    fx_to_enable.reserve(fx_to_disable.size());

    for (auto i = fx_disabled_g->begin(); i != fx_disabled_g->end();) {
        auto v = find(fx_to_disable.begin(), fx_to_disable.end(), *i);
        if (v != fx_to_disable.end()) {
            fx_to_disable.erase(v);
            ++i;
        }
        else {
            fx_to_enable.push_back(move(*i));
            i = fx_disabled_g->erase(i);
        }
    }

    if (!fx_to_enable.empty() || !fx_to_disable.empty()) {
        auto preventCount = fx_to_disable.size() + fx_to_enable.size() + 4;

        for (auto i = fx_safe.begin(); i != fx_safe.end();) {
            auto v = fx_map.find(*i);
            if (v == fx_map.end()) {
                i = fx_safe.erase(i);
            }
            else {
                ++i;
            }
        }

        PreventUIRefresh((int)preventCount);
        Undo_BeginBlock();
        SetGlobalAutomationOverride(6);

        for (auto&& i : fx_to_enable) {
            auto fx = fx_map.at(i);
            if (ValidatePtr2(0, fx.tr, "MediaTrack*")) {
                TrackFX_SetEnabled(fx.tr, fx.idx, true);
            }
        }

        for (auto&& i : fx_to_disable) {
            auto fx = fx_map.at(i);
            if (ValidatePtr2(0, fx.tr, "MediaTrack*")) {
                TrackFX_SetEnabled(fx.tr, fx.idx, false);
                fx_disabled_g->push_back(i);
            }
        }

        SetGlobalAutomationOverride(global_automation_override);
        Undo_EndBlock("ReaLlm: REAPER Low latency monitoring", UNDO_STATE_FX);
        PreventUIRefresh(-(int)preventCount);
        res = true;
    }
    return res;
}

static void get_set_state(FXResults& r, bool is_set = false)
{
    constexpr char extName[] = "ak5k";
    constexpr char key[] = "ReaLlm";
    constexpr char keySafe[] = "ReaLlmSafe";

    auto& fx_disabled = r.fx_disabled;
    auto& fx_safe = r.safe;

    if (is_set) {
        char buf[BUFSZGUID];
        string s;

        s.clear();
        for (auto&& i : fx_disabled) {
            guidToString(i, buf);
            s.append(buf);
            s.append(" ");
        }
        (void)SetProjExtState(0, extName, key, s.c_str());

        s.clear();
        for (auto&& i : fx_safe) {
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
            auto v = guid_string_map.find(k);
            if (v != guid_string_map.end()) {
                fx_safe.push_back(v->second);
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
            auto v = guid_string_map.find(k);
            if (v != guid_string_map.end()) {
                fx_disabled.push_back(v->second);
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

    vector<MediaTrack*> input_tracks {};

    initialize(input_tracks);

    auto project_state_change_count_now =
        GetProjectStateChangeCount(0) + global_automation_override;
    if (project_state_change_count_now != project_state_change_count) {
        project_state_change_count = project_state_change_count_now;
    }
    else {
        return;
    }

    FXResults fx_state {};

    pdc_max = 0;
    get_set_state(fx_state);
    fx_disabled_g = &fx_state.fx_disabled;

    if (exit != nullptr && *exit == true) {
        input_tracks.clear();
    }

    vector<future<FXResults>> v {};
    v.reserve(input_tracks.size());
    for (auto&& i : input_tracks) {
        v.emplace_back(async([i, fx_safe = fx_state.safe] {
            Network<MediaTrack*, FXResults, int> n {i};
            FXResults r;
            r.safe = std::move(fx_safe);
            n.set_results(r);
            n.traverse(true);
            return n.get_results();
        }));
    }

    for (auto&& i : v) {
        auto results = i.get();
        fx_state.to_disable.insert(
            fx_state.to_disable.end(),
            results.to_disable.begin(),
            results.to_disable.end());
        fx_state.safe.insert(
            fx_state.safe.end(),
            results.safe.begin(),
            results.safe.end());
        fx_state.unsafe.insert(
            fx_state.unsafe.end(),
            results.unsafe.begin(),
            results.unsafe.end());
    }

    set<GUID*> fx_to_disable_unique(
        fx_state.to_disable.cbegin(),
        fx_state.to_disable.cend());
    set<GUID*> fx_safe_unique(fx_state.safe.cbegin(), fx_state.safe.cend());
    set<GUID*> fx_unsafe_unique(
        fx_state.unsafe.cbegin(),
        fx_state.unsafe.cend());

    fx_state.safe.clear();
    fx_state.safe.resize(fx_safe_unique.size() - fx_unsafe_unique.size());
    set_difference(
        fx_safe_unique.cbegin(),
        fx_safe_unique.cend(),
        fx_unsafe_unique.cbegin(),
        fx_unsafe_unique.cend(),
        fx_state.safe.begin());

    fx_state.fx_disabled.insert(
        fx_state.fx_disabled.end(),
        fx_unsafe_unique.begin(),
        fx_unsafe_unique.end());

    if (process_fx(fx_to_disable_unique, fx_state.safe)) {
        get_set_state(fx_state, true);
    }

    if (exit != nullptr && *exit == true) {
        fx_map.clear();
        guid_string_map.clear();
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
            llm_state = !llm_state;
        }
        if (llm_state == 1) {
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
        return llm_state;
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
    vector<MediaTrack*> input_tracks {};

    FXResults fx_results {};
    get_set_state(fx_results);

    auto& fx_disabled = fx_results.fx_disabled;
    auto& fx_safe = fx_results.safe;

    if (strcmp(parmname, "P_REALLM") == 0 || strcmp(parmname, "P_STATE") == 0 ||
        strcmp(parmname, "P_VECTOR") == 0) {
        auto pdc_mode_check_temp = pdc_mode_check;
        pdc_mode_check = false;
        initialize(input_tracks);
        if (ValidatePtr2(0, tr, "MediaTrack*")) {
            input_tracks.push_back(tr);
        }
        vector<vector<MediaTrack*>> routes {};
        for (auto&& i : input_tracks) {
            Network<MediaTrack*, FXResults, int> n {i};
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
                            find(fx_disabled.cbegin(), fx_disabled.cend(), g);
                        if (v != fx_disabled.cend()) {
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
            Network<MediaTrack*, FXResults, int> n {GetTrack(0, i)};
            network.emplace(pair {n.get(), n.get_neighborhood(n.get())});
        }
        Network<MediaTrack*, FXResults, int> n {GetMasterTrack(0)};
        network.emplace(pair {n.get(), n.get_neighborhood(n.get())});
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
        initialize(input_tracks);
        for (auto i = fx_safe.begin(); i != fx_safe.end();) {
            auto v = fx_map.find(*i);
            if (v == fx_map.end()) {
                i = fx_safe.erase(i);
            }
            else {
                ++i;
            }
        }
        for (auto&& i : fx_safe) {
            s.append(to_string(fx_map.at(i).tr_idx()));
            s.append(":");
            s.append(to_string(fx_map.at(i).idx));
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

const char* defstring_Set =
    "void\0const char*,const char*\0"
    "parmname,"
    "bufIn\0"
    "Set ReaLlm parameters."
    "\n" //
    "P_PDCLIMIT: "
    "Latency limit in samples. Should be multiple of audio block/buffer "
    "size, in most cases. NOTE: Results depend on audio buffer size, and "
    "Track "
    "FX Chain PDC if P_PDCMODECHECK is enabled."
    "\n" //
    "P_PDCMODECHECK: "
    "Check Track FX Chain PDC mode during Llm_Do(). \"0\" or \"1\".";

void Set(const char* parmname, const char* buf)
{
    scoped_lock lock(m);

    if (strcmp(parmname, "P_PDCLIMIT") == 0) {
        pdc_limit = stoi(buf);
    }

    else if (strcmp(parmname, "P_PDCMODECHECK") == 0) {
        if (stoi(buf) == 0) {
            pdc_mode_check = false;
        }
        else if (stoi(buf) == 1) {
            pdc_mode_check = true;
        }
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
        llm_state = 0;

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
    }
    return;
}

} // namespace llm