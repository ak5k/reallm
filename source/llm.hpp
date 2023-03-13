#include <algorithm>
#include <cstring>
#include <limits.h>
#include <reaper_plugin_functions.h>
#include <regex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

constexpr auto BUFSZSMALL = 8;
constexpr auto BUFSZGUID = 64;
constexpr auto BUFSZCHUNK = 1024;
constexpr auto BUFSZNEEDBIG = 32768;

namespace llm {

extern std::unordered_map<std::string, GUID*> guid_string_map;

class FXState {
  public:
    std::vector<GUID*> fx_disabled;
    std::vector<GUID*> to_disable;
    std::vector<GUID*> safe;
    std::vector<GUID*> unsafe;
    int pdc;
    FXState()
        : pdc {}
    {
        fx_disabled.reserve(BUFSZSMALL);
        to_disable.reserve(BUFSZSMALL);
        safe.reserve(BUFSZSMALL);
        unsafe.reserve(BUFSZSMALL);
    }

    void prepare()
    {
        vector_clean_duplicates_and_sort(fx_disabled);
        vector_clean_duplicates_and_sort(to_disable);
        vector_clean_duplicates_and_sort(safe);
        vector_clean_duplicates_and_sort(unsafe);
        std::vector<GUID*> v;
        v.resize(safe.size() - unsafe.size());
        std::set_difference(
            safe.cbegin(),
            safe.cend(),
            unsafe.cbegin(),
            unsafe.cend(),
            v.begin());

        safe.assign(v.begin(), v.end());
        fx_disabled.insert(fx_disabled.end(), unsafe.begin(), unsafe.end());
        return;
    }

  private:
    template <typename T>
    void vector_clean_duplicates_and_sort(std::vector<T>& v)
    {
        std::unordered_set<T> s;
        for (auto&& i : v) {
            s.insert(i);
        }
        v.assign(s.begin(), s.end());
        sort(v.begin(), v.end());
        return;
    }
};

class Track {
  public:
    MediaTrack* tr;
    Track()
        : tr {}
    {
    }
    Track(MediaTrack* tr)
        : tr {tr}
    {
        track_map[tr] = std::move(*this);
    }

    static std::unordered_map<MediaTrack*, Track> track_map;

  private:
    char buf[BUFSZCHUNK] = {0};
};

class FX {

  public:
    MediaTrack* tr;
    int idx;
    GUID* g;
    FX()
        : tr {}
        , idx {}
        , g {}
        , tr_idx_ {INT_MAX}
    {
    }

    FX(MediaTrack* tr, int fx_idx, bool local = false)
        : tr {tr}
        , idx {fx_idx}
        , g {TrackFX_GetFXGUID(tr, fx_idx)}
        , tr_idx_ {INT_MAX}
    {
        if (!local) {
            if (fx_map[g].g == nullptr) {
                guidToString(g, buf);
                guid_string_map[std::string {buf}] = g;
            }
        }
        fx_map[g] = std::move(*this);
    }

    int tr_idx()
    {
        if (tr_idx_ == INT_MAX) {
            tr_idx_ = (int)GetMediaTrackInfo_Value(tr, "IP_TRACKNUMBER");
            if (tr_idx_ == 0) {
                return INT_MAX;
            }
            tr_idx_ = tr_idx_ - 1;
        }
        return tr_idx_;
    }

    static std::unordered_map<GUID*, FX> fx_map;
    // static std::unordered_map<GUID*, FX> fx_map;

  private:
    char buf[BUFSZGUID] = {0};
    int tr_idx_;
};

class FXExt : public FX {
  public:
    bool enabled;
    char name[BUFSZGUID] = {0};
    int pdc;
    FXExt()
        : FX {}
        , enabled {}
        , name {}
        , pdc {}
    {
    }
    FXExt(MediaTrack* tr, int fx_idx)
        : FX {tr, fx_idx, true}
        , enabled {TrackFX_GetEnabled(tr, fx_idx)}
        , name {}
        , pdc {}
    {
        TrackFX_GetFXName(tr, idx, name, BUFSZGUID);
        TrackFX_GetNamedConfigParm(tr, idx, "pdc", buf, BUFSZSMALL);
        if (strlen(buf) == 0) {
            strncpy(buf, "0", BUFSZSMALL);
        }
        pdc = std::atoi(buf);
        if (strstr(name, "ReaInsert")) {
            pdc = BUFSZNEEDBIG;
        }
        fx_map_ext[g] = std::move(*this);
    }

    static std::unordered_map<GUID*, FXExt> fx_map_ext;
    // static std::unordered_map<GUID*, FXExt> fx_map_ext;

  private:
    char buf[BUFSZGUID] = {0};
};

void Register(bool load);

} // namespace llm

#ifdef WIN32
// #ifdef _DEBUG
#include <WDL/win32_printf.h>
// #endif
#endif