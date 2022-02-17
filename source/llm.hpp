#include <algorithm>
#include <limits.h>
#include <reaper_plugin_functions.h>
// #include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define BUFSZCHUNK 1024
#define BUFSZGUID 64
#define BUFSZNEEDBIG 32768
#define BUFSZSMALL 8

namespace llm {

extern std::unordered_map<std::string, GUID*> guid_string_map;

class FXResults {
  public:
    std::vector<GUID*> fx_disabled;
    std::vector<GUID*> to_disable;
    std::vector<GUID*> safe;
    std::vector<GUID*> unsafe;
    int pdc;
    FXResults()
        // : fx_disabled {}
        // , to_disable {}
        // , safe {}
        // , unsafe {}
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

class FXBase {

  public:
    MediaTrack* tr;
    int idx;
    GUID* g;
    FXBase()
        : tr {}
        , idx {}
        , g {}
        , tr_idx_ {INT_MAX}
    {
    }

    FXBase(MediaTrack* tr, int fx_idx)
        : tr {tr}
        , idx {fx_idx}
        , g {TrackFX_GetFXGUID(tr, fx_idx)}
        , tr_idx_ {INT_MAX}
    {
        if (fx_map[g].g == nullptr) {
            guidToString(g, buf);
            guid_string_map[std::string {buf}] = g;
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

    static std::unordered_map<GUID*, FXBase> fx_map;

  private:
    char buf[BUFSZGUID];
    int tr_idx_;
};

class FX : public FXBase {
  public:
    bool enabled;
    int pdc;
    char name[BUFSZGUID];
    FX(MediaTrack* tr, int fx_idx)
        : FXBase {tr, fx_idx}
        , enabled {TrackFX_GetEnabled(tr, fx_idx)}
        , pdc {[this]() {
            TrackFX_GetNamedConfigParm(
                this->tr,
                this->idx,
                "pdc",
                this->buf,
                BUFSZSMALL);
            return std::atoi(buf);
        }()}
    {
        TrackFX_GetFXName(this->tr, this->idx, this->name, BUFSZGUID);
    }

  private:
    char buf[BUFSZGUID];
};

void Register(bool load);

} // namespace llm