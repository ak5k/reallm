#include "llm.hpp"
#include "node.hpp"
#include <atomic>
#include <cmath>
#include <cstring>
#include <future>
#include <reaper_plugin_functions.h>
#include <reascript_vararg.hpp>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>

// isolate llm into its own safe space
namespace llm {
// 'import' stl stuff
using namespace std;

// globals
static atomic<int> llm_state {};
static double reaper_version {};
static int bsize {};
static int command_id {};
static int global_automation_override {};
static int pdc_limit_abs {};
static int pdc_limit {1};
static int pdc_max {};
static int project_state_change_count {0};

static MediaTrack* master_track {};
static vector<GUID*>* fx_disabled_g {};

unordered_map<string, GUID*> guid_string_map {};

// master lock for thread safety
static mutex m {};

// help IDE to know types while deving
template class Network<MediaTrack*, FXState, int>;

// initialize static members
unordered_map<GUID*, FX> FX::fx_map {};
unordered_map<GUID*, FXExt> FXExt::fx_map_ext {};
unordered_map<MediaTrack*, Track> Track::track_map {};

// // assign aliases
unordered_map<GUID*, FX>& fx_map {FX::fx_map};
unordered_map<GUID*, FXExt>& fx_map_ext {FXExt::fx_map_ext};
unordered_map<MediaTrack*, Track>& track_map {Track::track_map};

template <typename T, typename U, typename V>
std::vector<T> Network<T, U, V>::get_neighborhood(T& k)
{
    std::vector<T> v;
    auto num_sends = GetTrackNumSends(k, 0);
    v.reserve(num_sends + 1);
    auto neighbor = GetParentTrack(k);
    auto link = (bool)GetMediaTrackInfo_Value(k, "B_MAINSEND");

    if (neighbor && link) {
        v.push_back(neighbor);
    }

    else if (!neighbor && link && k != master_track) {
        v.push_back(master_track);
    }

    for (auto i = 0; i < num_sends; i++) {
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
    auto& fx_map_ext = FXExt().fx_map_ext;
    auto& fx_to_disable = r.to_disable;
    auto& fx_safe = r.safe;
    auto& fx_unsafe = r.unsafe;
    auto& pdc_current = v;
    auto& track_map = Track().track_map;
    auto pdc_temp {0};
    auto pdc_mode {-1};

    if (!ValidatePtr2(0, tr, "MediaTrack*")) {
        return v;
    }

    auto instrument = TrackFX_GetInstrument(tr);

    for (auto i = 0; i < TrackFX_GetCount(tr); i++) {
        auto guid = TrackFX_GetFXGUID(tr, i);
        auto& fx = fx_map_ext[guid];
        if (fx.tr_idx() == INT_MAX) {
            fx = FXExt {tr, i};
        }
        auto& pdc = fx.pdc;
        pdc_mode = fx.pdc_mode;

        auto& is_enabled = fx.enabled;

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
                fx_unsafe.push_back(*v);
                was_disabled = true;
            }
        }

        if (!is_enabled && !was_disabled) {
            pdc = 0;
        }

        if (pdc > 0) {
            pdc_temp = pdc_temp + pdc;
            if (!safe && pdc_current + pdc_temp > pdc_limit_abs) {
                pdc_temp = pdc_temp - pdc;
                auto k =
                    find(fx_to_disable.cbegin(), fx_to_disable.cend(), guid);
                if (k == fx_to_disable.cend()) {
                    fx_to_disable.push_back(guid);
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
    master_track = GetMasterTrack(0);
    v.reserve(BUFSZSMALL);
    for (auto i = 0; i < GetNumTracks() + 1; i++) {
        auto tr = GetTrack(0, i);
        if (!tr) {
            tr = master_track;
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
            FX {tr, j};
        }
    }
    return;
}

static bool process_fx(vector<GUID*>& fx_to_disable, vector<GUID*>& fx_safe)
{
    auto res = false;
    vector<GUID*> fx_to_enable {};
    fx_to_enable.reserve(fx_to_disable.size());

    for (auto i = fx_disabled_g->cbegin(); i != fx_disabled_g->cend();) {
        auto v = find(fx_to_disable.cbegin(), fx_to_disable.cend(), *i);
        if (v != fx_to_disable.end()) {
            fx_to_disable.erase(v);
            ++i;
        }
        else {
            fx_to_enable.push_back(*i);
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
        unordered_map<MediaTrack*, int> tracks_to_enable {};
        unordered_map<MediaTrack*, int> tracks_to_disable {};

        for (auto&& i : fx_to_enable) {
            auto fx = fx_map.at(i);
            if (ValidatePtr2(0, fx.tr, "MediaTrack*")) {
                TrackFX_SetEnabled(fx.tr, fx.idx, true);
                tracks_to_enable.insert_or_assign(fx.tr, fx.idx);
            }
        }

        for (auto&& i : fx_to_disable) {
            auto fx = fx_map.at(i);
            if (ValidatePtr2(0, fx.tr, "MediaTrack*")) {
                TrackFX_SetEnabled(fx.tr, fx.idx, false);
                fx_disabled_g->push_back(i);
                tracks_to_disable.insert_or_assign(fx.tr, fx.idx);
            }
        }

        // for (auto&& i : tracks_to_enable) {
        //     TrackFX_SetNamedConfigParm(i.first, i.second, "pdc_mode", "1");
        // }

        // for (auto&& i : tracks_to_disable) {
        //     TrackFX_SetNamedConfigParm(i.first, i.second, "pdc_mode", "2");
        // }

        SetGlobalAutomationOverride(global_automation_override);
        Undo_EndBlock("ReaLlm: REAPER Low latency monitoring", UNDO_STATE_FX);
        PreventUIRefresh(-(int)preventCount);
        res = true;
    }
    return res;
}

static void get_set_state(FXState& r, bool is_set = false)
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
            if (ValidatePtr2(0, i, "GUID*")) {
                guidToString(i, buf);
                if (buf[0] != '\0') {
                    s.append(buf);
                    s.append(" ");
                }
            }
        }
        (void)SetProjExtState(0, extName, key, s.c_str());

        s.clear();
        for (auto&& i : fx_safe) {
            if (ValidatePtr2(0, i, "GUID*")) {
                guidToString(i, buf);
                if (buf[0] != '\0') {
                    s.append(buf);
                    s.append(" ");
                }
            }
        }
        (void)SetProjExtState(0, extName, keySafe, s.c_str());
    }
    else {
        string k {};
        stringstream ss {};

        size_t bufSz = BUFSZNEEDBIG;
        char* p = nullptr;
        while (true) {
            auto tmp = (char*)realloc(p, sizeof(char) * bufSz);
            if (tmp) {
                p = tmp;
                break;
            }
        }

        (void)GetProjExtState(0, extName, keySafe, p, (int)bufSz);
        while (p && strlen(p) + 1 == bufSz) {
            bufSz = bufSz * 2;
            while (true) {
                auto tmp = (char*)realloc(p, sizeof(char) * bufSz);
                if (tmp) {
                    p = tmp;
                    break;
                }
            }
            (void)GetProjExtState(0, extName, keySafe, p, (int)bufSz);
        }

        if (p) {
            ss.str(p);
            while (ss >> k) {
                auto v = guid_string_map.find(k);
                if (v != guid_string_map.end()) {
                    fx_safe.push_back(v->second);
                }
            }
        }

        (void)GetProjExtState(0, extName, key, p, (int)bufSz);
        while (p && strlen(p) + 1 == bufSz) {
            bufSz = bufSz * 2;
            while (true) {
                auto tmp = (char*)realloc(p, sizeof(char) * bufSz);
                if (tmp) {
                    p = tmp;
                    break;
                }
            }
            (void)GetProjExtState(0, extName, key, p, (int)bufSz);
        }

        if (p) {
            k.clear();
            ss.clear();
            ss.str(p);
            while (ss >> k) {
                auto v = guid_string_map.find(k);
                if (v != guid_string_map.end()) {
                    fx_disabled.push_back(v->second);
                }
            }
        }
        free(p);
    }
    return;
}

const char* defstring_Do =
    "void\0int*\0paramInOptional\0"
    "Called with parameter value 1 executes one ReaLlm cycle. "
    "E.g. for running ReaLlm on custom timer, or deferred. "
    "0 or nothing performs shutdown. "
    "Disarming/disabling all monitored inputs and calling with parameter value "
    "1 equals to shutdown.";

bool timer {false};

static void Do();

static void Do2(int* param = 0)
{
    if (param != 0 && (*param == 1)) {
        timer = true;
    }
    else {
        timer = false;
    }
    Do();
}

static void Do()
{
    while (true) {
        // auto time0 = time_precise();
        auto project_state_change_count_now =
            GetProjectStateChangeCount(0) + global_automation_override;
        if (project_state_change_count_now != project_state_change_count) {
            project_state_change_count = project_state_change_count_now;
        }
        else if (timer) {
            return;
        }
        scoped_lock lk(m);
        auto llm_state_current = llm_state.load();

        reaper_version = stod(GetAppVersion());
        char buf[BUFSZSMALL];
        if (GetAudioDeviceInfo("BSIZE", buf, BUFSZSMALL)) {
            bsize = stoi(buf);
            pdc_limit_abs = pdc_limit * bsize;
        }

        vector<MediaTrack*> input_tracks {};

        initialize(input_tracks);

        FXState fx_state {};

        pdc_max = 0;
        get_set_state(fx_state);
        fx_disabled_g = &fx_state.fx_disabled;

        if (llm_state_current == 0) {
            input_tracks.clear();
        }

        // vector<future<FXState>> v {};

        for (auto&& i : input_tracks) {
            Network<MediaTrack*, FXState, int> n {i, fx_state};
            n.traverse(true);
        }

        fx_state.prepare();

        if (process_fx(fx_state.to_disable, fx_state.safe)) {
            get_set_state(fx_state, true);
        }

        FXExt fx;
        fx.fx_map_ext.clear();
        Track tr_thread;
        tr_thread.track_map.clear();

#ifdef WIN32
        // auto time1 = time_precise() - time0;
        // #ifdef _DEBUG
        // wdl_printf("%f%s", time1, "\n");
// #endif
#endif
        if (timer || llm_state_current == 0) {
            break;
        }
    }

    if (!timer) {
        FXExt fx;
        fx.fx_map.clear();
        guid_string_map.clear();
    }

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
        llm_state = !llm_state;
        // static int param = llm_state;
        if (llm_state == 1) {
            plugin_register("timer", (void*)&Do);
        }
        else {
            plugin_register("-timer", (void*)&Do);
            timer = false;
            Do(); // exitInOptional is true
            guid_string_map.clear();
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
    scoped_lock lk(m);
    string s {};
    vector<MediaTrack*> input_tracks {};

    FXState fx_state {};
    get_set_state(fx_state);

    auto& fx_disabled = fx_state.fx_disabled;
    auto& fx_safe = fx_state.safe;

    if (strcmp(parmname, "P_REALLM") == 0 || strcmp(parmname, "P_STATE") == 0 ||
        strcmp(parmname, "P_VECTOR") == 0) {
        initialize(input_tracks);
        if (ValidatePtr2(0, tr, "MediaTrack*")) {
            input_tracks.push_back(tr);
        }
        vector<vector<MediaTrack*>> routes {};
        for (auto&& i : input_tracks) {
            Network<MediaTrack*, FXState, int> n {i, fx_state};
            n.traverse(false);
            for (auto&& i : n.get_routes()) {
                routes.push_back(i);
            }
        }
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
            Network<MediaTrack*, FXState, int> n {GetTrack(0, i), fx_state};
            network.emplace(pair {n.get(), n.get_neighborhood(n.get())});
        }
        Network<MediaTrack*, FXState, int> n {master_track, fx_state};
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
    "PDC latency limit in audio blocks/buffers."
    "\n" //
    ;

void Set(const char* parmname, const char* buf)
{
    scoped_lock lk(m);

    if (strcmp(parmname, "P_PDCLIMIT") == 0) {
        pdc_limit = stoi(buf);
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

        plugin_register("API_Llm_Do", (void*)&Do2);
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

        plugin_register("-API_Llm_Do", (void*)&Do2);
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