#include "Node.hpp"
#include <reaper_plugin_functions.h>

template <>
std::unordered_set<Node<MediaTrack*>*> Node<MediaTrack*>::network;

template <>
std::unordered_set<Node<MediaTrack*>*>& Node<MediaTrack*>::neighborhood()
{
    _neighborhood.clear();
    auto _neighbor = GetParentTrack(_node);
    auto link = (bool)GetMediaTrackInfo_Value(_node, "B_MAINSEND");

    if (_neighbor && link) {
        _neighborhood.emplace(_neighbor);
    }

    // else if (!_neighbor && link && _node != GetMasterTrack(0)) {
    //     _neighborhood.emplace(Node<MediaTrack*>(GetMasterTrack(0)).get());
    // }

    // for (auto i = 0; i < GetTrackNumSends(_node, 0); i++) {
    //     auto mute = (bool)GetTrackSendInfo_Value(_node, 0, i, "B_MUTE");
    //     _neighbor = (MediaTrack*)(uintptr_t)
    //         GetTrackSendInfo_Value(_node, 0, i, "P_DESTTRACK");
    //     if (!mute) {
    //         _neighborhood.emplace(Node<MediaTrack*>(_neighbor).get());
    //     }
    // }
    return _neighborhood;
}