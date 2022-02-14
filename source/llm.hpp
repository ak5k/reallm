#include <mutex>
#include <reaper_plugin_functions.h>
#include <vector>

#define BUFSZCHUNK 1024
#define BUFSZGUID 64
#define BUFSZNEEDBIG 32768
#define BUFSZSMALL 8

namespace llm {

struct TrackFX {
    MediaTrack* tr;
    int tr_idx;
    GUID* g;
    int fx_idx;
    TrackFX(MediaTrack* tr, int tr_idx, GUID* g, int fx_idx)
        : tr(tr)
        , tr_idx(tr_idx)
        , g(g)
        , fx_idx(fx_idx)
    {
    }
};

void Register(bool load);

} // namespace llm