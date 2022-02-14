#include "llm.hpp"
#include "node.hpp"
#include <reaper_plugin_functions.h>

template class Node<MediaTrack*, GUID*, int>;

template <typename T, typename U, typename V>
std::unordered_set<T>& Node<T, U, V>::neighborhood()
{
    _neighborhood.clear();
    auto _neighbor = GetParentTrack(GetMasterTrack(0));
    auto link = (bool)GetMediaTrackInfo_Value(_node, "B_MAINSEND");

    if (_neighbor && link) {
        _neighborhood.insert(_neighbor);
    }

    else if (!_neighbor && link && _node != GetMasterTrack(0)) {
        _neighborhood.insert(GetMasterTrack(0));
    }

    for (auto i = 0; i < GetTrackNumSends(_node, 0); i++) {
        auto mute = (bool)GetTrackSendInfo_Value(_node, 0, i, "B_MUTE");
        _neighbor = (MediaTrack*)(uintptr_t)
            GetTrackSendInfo_Value(_node, 0, i, "P_DESTTRACK");
        if (!mute) {
            _neighborhood.insert(_neighbor);
        }
    }
    return _neighborhood;
}

template <typename T, typename U, typename V>
V& Node<T, U, V>::analyze(T& k, V& v)
{
    // auto pdc_mode {-1};
    // auto pdc_temp {0};

    // if (reaperVersion < 6.20) {
    //     pdcMode = 0;
    // }

    // if (pdcModeCheck == true && reaperVersion > 6.19) {
    //     auto v = pdcModeMap.find(tr);
    //     if (v != pdcModeMap.end()) {
    //         pdcMode = v->second;
    //     }
    //     else {
    char buf[BUFSZCHUNK];
    (void)GetTrackStateChunk(k, buf, BUFSZCHUNK, false);
    // const regex re("PDC_OPTIONS (\\d+)");
    // cmatch match;
    // regex_search(buf, match, re);
    // string s = string(match[1]);
    // if (s == "0" || s == "2") {
    //     pdc_mode = stoi(s);
    // }
    // //     }
    // // }

    // const auto instrument = TrackFX_GetInstrument(tr);

    // for (auto i = 0; i < TrackFX_GetCount(tr); i++) {
    //     auto pdc {0};
    //     // auto trPdc = pdcMap.find(tr);
    //     // if (trPdc != pdcMap.end()) {
    //     //     auto fxPdc = trPdc->second.find(i);
    //     //     if (fxPdc != trPdc->second.end()) {
    //     //         pdc = fxPdc->second;
    //     //     }
    //     // }
    //     // else {
    //     char bufPdc[BUFSZSMALL];
    //     (void)TrackFX_GetNamedConfigParm(tr, i, "pdc", bufPdc, BUFSZSMALL);
    //     char bufName[BUFSZGUID];
    //     (void)TrackFX_GetFXName(tr, i, bufName, BUFSZGUID);
    //     if (string(bufName).find("ReaInsert") != string::npos) {
    //         (void)strncpy(bufPdc, "32768", BUFSZSMALL);
    //     }
    //     if (strlen(bufPdc) == 0) {
    //         (void)strncpy(bufPdc, "0", BUFSZSMALL);
    //     }
    //     pdc = stoi(bufPdc);
    //     // }

    //     const auto isEnabled = TrackFX_GetEnabled(tr, i);
    //     const auto guid = TrackFX_GetFXGUID(tr, i);

    //     auto wasDisabled = false;
    //     if (find(fxDisabled.begin(), fxDisabled.end(), guid) !=
    //         fxDisabled.end()) {
    //         wasDisabled = true; // previously disabled by llm
    //     }

    //     auto safe = false;
    //     if ((isEnabled && wasDisabled) || i == instrument) {
    //         fxSafe.push_back(guid);
    //         safe = true;
    //     }
    //     else if (find(fxSafe.begin(), fxSafe.end(), guid) != fxSafe.end()) {
    //         safe = true;
    //     }

    //     if (!isEnabled) {
    //         auto k = find(fxSafe.begin(), fxSafe.end(), guid);
    //         if (k != fxSafe.end()) {
    //             safe = false;
    //             fxSafe.erase(k);
    //             wasDisabled = true;
    //         }
    //     }

    //     if (!isEnabled && !wasDisabled) {
    //         pdc = 0;
    //     }

    //     if (pdc > 0) {
    //         if (pdcMode == 0) {
    //             pdc = (1 + (pdc / bsize)) * bsize;
    //         }
    //         pdcTemp = pdcTemp + pdc;
    //         if (!safe) {
    //             if (pdcMode == -1 &&
    //                 (pdcCurrent + (int)(ceil((double)pdcTemp / bsize)) *
    //                 bsize >
    //                  pdcLimit)) {
    //                 pdcTemp = pdcTemp - pdc;
    //                 auto k = find(fxToDisable.begin(), fxToDisable.end(),
    //                 guid); if (k == fxToDisable.end()) {
    //                     fxToDisable.push_back(guid);
    //                 }
    //             }
    //             else if (
    //                 (pdcMode == 0 || pdcMode == 2) &&
    //                 pdcCurrent + pdcTemp > pdcLimit) {
    //                 pdcTemp = pdcTemp - pdc;
    //                 auto k = find(fxToDisable.begin(), fxToDisable.end(),
    //                 guid); if (k == fxToDisable.end()) {
    //                     fxToDisable.push_back(guid);
    //                 }
    //             }
    //         }
    //     }
    // if (pdcMode == -1 && pdcTemp > 0) {
    //     pdcCurrent = pdcCurrent + (int)(ceil((double)pdcTemp / bsize)) *
    //     bsize;
    // }
    // else if (pdcTemp > 0) {
    //     pdcCurrent = pdcCurrent + pdcTemp;
    // }

    // if (pdcCurrent > pdcMax) {
    //     pdcMax = pdcCurrent;
    // }

    return v;
}

namespace llm {

// typedef ::Node<MediaTrack*, GUID*, int> Node;

bool pdc_mode_check {};
int command_id {};
int pdc_limit {};
int pdc_max {};
int state {};
std::mutex m {};

void Register(bool load)
{
    (void)load;
    return;
}

} // namespace llm