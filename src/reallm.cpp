#include "reallm.hpp"
#include "Network.hpp"
#include "reaper_vararg.hpp"
#include <reaper_plugin_functions.h>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace reallm
{

int command_id{0};
bool toggle_action_state{false};
constexpr auto command_name = "AK5K_REALLM";
constexpr auto action_name = "ReaLlm: REAPER Low latency monitoring";
custom_action_register_t action = {0, command_name, action_name, nullptr};
void main();

int pdc_limit;
int bsize;
double pdc_factor{1.0};
bool shutdown{false};
bool include_monitoring_fx{false};
bool keep_pdc{false};

Network<MediaTrack*> network;
std::vector<MediaTrack*> inputTracks;
std::vector<MediaTrack*> outputTracks;
std::unordered_set<std::string> safed_fx_names;

constexpr auto prefix_string = "llm ";

struct ParameterChange
{
  ParameterChange(const char* fx_name, int parameter_index, double val1,
                  double val2)
    : fx_name(fx_name), parameter_index(parameter_index), val1(val1), val2(val2)
  {
  }

  std::string fx_name;
  int parameter_index;
  double val1;
  double val2;
};

std::vector<ParameterChange> parameter_changes;

class TrackFx
{
public:
  TrackFx() : g(nullptr), tr(nullptr), fx_index(-1), isSafe(false), buf{}
  {
  }

  TrackFx(GUID* g, MediaTrack* tr, int fx_index)
    : g(g), tr(tr), fx_index(fx_index), isSafe(false), buf{}
  {
  }

  void enable()
  {
    if (name.empty())
    {
      TrackFX_GetFXName(tr, fx_index, buf, BUFSIZ);
      name = buf;
    }
    auto parameter_changed = false;
    for (auto&& i : parameter_changes)
    {
      if (name.find(i.fx_name) != std::string::npos)
      {
        TrackFX_SetParamNormalized(tr, fx_index, i.parameter_index, i.val2);
        parameter_changed = true;
      }
    }
    if (parameter_changed)
    {
      return;
    }
    TrackFX_SetEnabled(tr, fx_index, true);
    TrackFX_GetNamedConfigParm(tr, fx_index, "renamed_name", buf, BUFSIZ);
    std::string str = buf;
    std::string substr = prefix_string;
    size_t pos = str.find(substr);
    if (pos != std::string::npos)
    {
      // The string contains the substring, so remove it
      str.erase(pos, substr.size());
    }
    TrackFX_GetNamedConfigParm(tr, fx_index, "fx_name", buf, BUFSIZ);
    if (str == buf)
    {
      TrackFX_SetNamedConfigParm(tr, fx_index, "renamed_name", "");
    }
    else
    {
      TrackFX_SetNamedConfigParm(tr, fx_index, "renamed_name", str.c_str());
    }
  }

  void disable()
  {
    if (name.empty())
    {
      TrackFX_GetFXName(tr, fx_index, buf, BUFSIZ);
      name = buf;
    }
    auto parameter_changed = false;
    for (auto&& i : parameter_changes)
    {
      if (name.find(i.fx_name) != std::string::npos)
      {
        TrackFX_SetParamNormalized(tr, fx_index, i.parameter_index, i.val1);
        parameter_changed = true;
      }
    }
    if (parameter_changed)
    {
      return;
    }
    TrackFX_SetEnabled(tr, fx_index, false);
    std::string str;
    TrackFX_GetNamedConfigParm(tr, fx_index, "renamed_name", buf, BUFSIZ);
    str = buf;
    if (str.empty())
    {
      TrackFX_GetNamedConfigParm(tr, fx_index, "fx_name", buf, BUFSIZ);
      str = buf;
    }
    std::string prefix = prefix_string;
    if (!(str.substr(0, prefix.size()) == prefix))
    {
      TrackFX_SetNamedConfigParm(tr, fx_index, "renamed_name",
                                 (prefix + str).c_str());
    }
  }

  int getPdc() const
  {
    char pdcBuf[BUFSIZ];
    TrackFX_GetNamedConfigParm(tr, fx_index, "pdc", pdcBuf, BUFSIZ);
    auto pdc = pdcBuf[0] != '\0' ? std::stoi(pdcBuf) : 0;
    return pdc;
  }

  bool getSafe() const
  {
    return isSafe;
  }

  void setSafe(bool isSafe)
  {
    this->isSafe = isSafe;
  }

  GUID* getGuid() const
  {
    return g;
  }

  std::string getGuidString() const
  {
    char guidBuf[BUFSIZ];
    if (ValidatePtr2(0, tr, "MediaTrack*"))
    {
      guidToString(g, guidBuf);
      return guidBuf;
    }
    return "";
  }

  bool getEnabled() const
  {
    return TrackFX_GetEnabled(tr, fx_index);
  }

  MediaTrack* getTrack() const
  {
    return tr;
  }

  int getFxIndex() const
  {
    return fx_index;
  }

private:
  GUID* g{};
  MediaTrack* tr;
  int fx_index;
  bool isSafe;
  char buf[BUFSIZ];
  std::string name;
};

std::unordered_map<GUID*, TrackFx> fx_map;
std::unordered_set<TrackFx*> fx_set_prev;

auto ToggleActionCallback(int command) -> int
{
  if (command != command_id)
  {
    return -1;
  }
  if (toggle_action_state)
  {
    return 1;
  }
  return 0;
}

auto OnAction(KbdSectionInfo* sec, int command, int val, int valhw, int relmode,
              HWND hwnd) -> bool
{
  // treat unused variables 'pedantically'
  (void)sec;
  (void)val;
  (void)valhw;
  (void)relmode;
  (void)hwnd;

  // check command
  if (command != command_id)
  {
    return false;
  }

  toggle_action_state = !toggle_action_state;

  if (toggle_action_state) // if toggle_action_state == true
  {
    // "reaper.defer(main)"
    plugin_register("timer", (void*)main);
  }
  else
  {
    // "reaper.atexit(shutdown)"
    plugin_register("-timer", (void*)main);
    shutdown = true;
    main();
    // shutdown stuff
  }
  return true;
}

} // namespace reallm

namespace reallm
{

std::vector<MediaTrack*> GetAllTrackSendDestinations(MediaTrack* sourceTrack)
{
  std::vector<MediaTrack*> destinationTracks;
  int numSends = GetTrackNumSends(sourceTrack, 0);

  for (int j = 0; j < numSends; j++)
  {
    MediaTrack* destinationTrack{nullptr};
    destinationTrack = (MediaTrack*)(UINT_PTR)GetTrackSendInfo_Value(
      sourceTrack, 0, j, "P_DESTTRACK");
    bool isSendMuted =
      (bool)GetTrackSendInfo_Value(sourceTrack, 0, j, "B_MUTE");
    if (!isSendMuted)
    {
      destinationTracks.push_back(destinationTrack);
    }
  }

  // Check if the track has a parent track
  MediaTrack* parentTrack =
    (MediaTrack*)GetSetMediaTrackInfo(sourceTrack, "P_PARTRACK", NULL);
  if (parentTrack != NULL)
  {
    // Check if the parent send is active
    if (*(bool*)GetSetMediaTrackInfo(parentTrack, "B_MAINSEND", NULL))
    {
      destinationTracks.push_back(parentTrack);
    }
  }
  else
  {
    // If the track has no parent, check if it has an active master send
    if (*(bool*)GetSetMediaTrackInfo(sourceTrack, "B_MAINSEND", NULL))
    {
      // Get the master track
      MediaTrack* masterTrack = GetMasterTrack(NULL);
      destinationTracks.push_back(masterTrack);
    }
  }

  return destinationTracks;
}

void dfs(Network<MediaTrack*>& network, MediaTrack* currentTrack,
         MediaTrack* targetTrack, std::vector<MediaTrack*>& visited,
         std::vector<MediaTrack*>& path,
         std::vector<std::vector<MediaTrack*>>& allPaths)
{
  visited.push_back(currentTrack);
  path.push_back(currentTrack);

  if (currentTrack == targetTrack)
  {
    allPaths.push_back(path);
  }
  else
  {
    for (auto&& nextTrack : network.getNodes()[currentTrack])
    {
      if (std::find(visited.begin(), visited.end(), nextTrack) == visited.end())
      {
        dfs(network, nextTrack, targetTrack, visited, path, allPaths);
      }
    }
  }

  path.pop_back();
}

std::vector<std::vector<MediaTrack*>> findAllPaths(
  Network<MediaTrack*>& network, std::vector<MediaTrack*>& inputTracks,
  std::vector<MediaTrack*>& outputTracks)
{
  std::vector<std::vector<MediaTrack*>> allPaths;

  for (auto&& inputTrack : inputTracks)
  {
    for (auto&& outputTrack : outputTracks)
    {
      std::vector<MediaTrack*> visited;
      std::vector<MediaTrack*> path;
      dfs(network, inputTrack, outputTrack, visited, path, allPaths);
    }
  }

  return allPaths;
}

// NOLINTNEXTLINE
int CalculateTrackPdc(MediaTrack* tr, int initial_pdc,
                      std::unordered_set<TrackFx*>& fx_set)
{
  char buf[BUFSIZ];
  TrackFX_GetNamedConfigParm(tr, 0, "chain_pdc_mode", buf, BUFSIZ);
  auto pdc_mode = buf[0] != '\0' ? std::stoi(buf) : 0;
  int pdc = initial_pdc;
  int tr_pdc = 0;
  auto fx_count = TrackFX_GetCount(tr);
  if (include_monitoring_fx && tr == GetMasterTrack(NULL))
  {
    fx_count = fx_count + TrackFX_GetRecCount(tr);
  }
  for (int j = 0; j < fx_count; j++)
  {
    auto i = j;
    if (tr == GetMasterTrack(0) && include_monitoring_fx &&
        i >= TrackFX_GetCount(tr))
    {
      i = i - TrackFX_GetCount(tr) + 0x1000000;
    }
    auto* g = TrackFX_GetFXGUID(tr, i);
    auto fx_pdc = fx_map[g].getPdc();
    auto flicker{false};
    if (fx_set_prev.find(&fx_map[g]) != fx_set_prev.end() && fx_pdc == 0 &&
        !TrackFX_GetEnabled(tr, i))
    {
      fx_set.insert(&fx_map[g]);
      flicker = true;
    }
    if (fx_set_prev.find(&fx_map[g]) == fx_set_prev.end() &&
        !TrackFX_GetEnabled(tr, i))
    {
      fx_pdc = 0;
    }
    if (!flicker && fx_pdc == 0)
    {
      continue;
    }
    if (fx_set_prev.find(&fx_map[g]) != fx_set_prev.end() &&
        fx_map[g].getSafe() && !TrackFX_GetEnabled(tr, i))
    {
      fx_map[g].setSafe(false);
    }
    else if (fx_set_prev.find(&fx_map[g]) != fx_set_prev.end() &&
             !fx_map[g].getSafe() && TrackFX_GetEnabled(tr, i))
    {
      fx_map[g].setSafe(true);
      // fx_set_prev.erase(&fx_map[g]);
    }
    if (pdc_mode == 0)
    {
      fx_pdc = fx_pdc % bsize == 0 ? fx_pdc : (fx_pdc / bsize + 1) * bsize;
      if (pdc + fx_pdc > pdc_limit && !fx_map[g].getSafe())
      {
        fx_set.insert(&fx_map[g]);
      }
      else
      {
        pdc += fx_pdc;
      }
    }
    if (pdc_mode == 1)
    {
      tr_pdc += fx_pdc;
      auto temp = tr_pdc % bsize == 0 ? tr_pdc : (tr_pdc / bsize + 1) * bsize;
      if (pdc + temp > pdc_limit && !fx_map[g].getSafe())
      {
        tr_pdc -= fx_pdc;
        fx_set.insert(&fx_map[g]);
      }
    }
    if (pdc_mode == 2)
    {
      pdc += fx_pdc;
      if (pdc > pdc_limit && !fx_map[g].getSafe())
      {
        pdc -= fx_pdc;
        fx_set.insert(&fx_map[g]);
      }
    }
  }
  if (pdc_mode == 1)
  {
    auto temp = tr_pdc % bsize == 0 ? tr_pdc : (tr_pdc / bsize + 1) * bsize;
    pdc += temp;
  }

  return pdc;
}

std::string serializeFxSet(std::unordered_set<TrackFx*>& fx_to_disable)
{
  std::string result;

  for (auto&& fx : fx_to_disable)
  {
    if (fx->getGuid() == nullptr)
    {
      continue;
    }
    // Convert each TrackFx to a string
    std::string fx_string = fx->getGuidString() + ",";
    if (fx->getSafe())
    {
      fx_string += "1";
    }
    else
    {
      fx_string += "0";
    }
    // Add the TrackFx string to the result
    if (!result.empty())
    {
      result += ";";
    }
    result += fx_string;
  }

  return result;
}

std::unordered_set<TrackFx*> deserializeFxSet(const std::string& serialized)
{
  std::unordered_set<TrackFx*> result;
  std::istringstream ss(serialized);
  std::string token;

  while (std::getline(ss, token, ';'))
  {
    std::istringstream tokenStream(token);
    std::string guidString;
    std::string safeString;

    std::getline(tokenStream, guidString, ',');
    std::getline(tokenStream, safeString, ',');
    GUID* g{nullptr};
    for (auto&& i : fx_map)
    {
      if (i.second.getGuidString() == guidString)
      {
        g = i.second.getGuid();
        fx_map[g].setSafe(safeString == "1");
        result.insert(&fx_map[g]);
        break;
      }
    }
  }

  return result;
}

const char* defstring_Do =
  "void\0\0\0"
  "Do. Call this function to run one ReaLlm cycle. Use this function to run "
  "ReaLlm on arbitrary time intervals e.g. from a deferred script.";

// NOLINTNEXTLINE
void main()
{
  // check if project state has changed
  if (!Audio_IsRunning)
  {
    return;
  }

  // get audio device buffer size
  auto start_time = time_precise();

  // get pdc limit
  char buf[BUFSIZ];
  GetAudioDeviceInfo("BSIZE", buf, BUFSIZ);
  bsize = buf[0] != '\0' ? std::stoi(buf) : 0;
  pdc_limit = (int)(bsize * abs(pdc_factor));

  // build network
  network.clear();
  inputTracks.clear();
  outputTracks.clear();
  static std::unordered_set<TrackFx*> possibleOrphans;
  possibleOrphans.clear();
  auto num_tracks = GetNumTracks();
  for (int i = 0; i < num_tracks + 1; i++)
  {
    // get track
    MediaTrack* tr = nullptr;
    if (i == num_tracks)
    {
      tr = GetMasterTrack(NULL);
    }
    else
    {
      tr = GetTrack(0, i);
    }
    network.addNode(tr);
    auto v = GetAllTrackSendDestinations(tr);
    for (auto&& dest : v)
    {
      network.addLink(tr, dest);
    }

    // get output tracks
    if (GetTrackNumSends(tr, 1) > 0)
    {
      outputTracks.push_back(tr);
    }

    // get input tracks
    auto i_recarm = *(int*)GetSetMediaTrackInfo(tr, "I_RECARM", NULL);
    auto i_recmon = *(int*)GetSetMediaTrackInfo(tr, "I_RECMON", NULL);
    auto tr_auto_mode = GetTrackAutomationMode(tr);
    if ((i_recarm != 0 && i_recmon != 0) ||
        (tr_auto_mode > 1 && tr_auto_mode < 6))
    {
      inputTracks.push_back(tr);
    }

    // get fx
    auto fx_count = TrackFX_GetCount(tr);
    if (i == num_tracks && include_monitoring_fx)
    {
      fx_count = fx_count + TrackFX_GetRecCount(tr);
    }
    for (int j = 0; j < fx_count; j++)
    {
      auto idx = j;
      if (i == num_tracks && include_monitoring_fx &&
          idx >= TrackFX_GetCount(tr))
      {
        idx = idx - TrackFX_GetCount(tr) + 0x1000000;
      }
      auto g = TrackFX_GetFXGUID(tr, idx);
      fx_map[g] = TrackFx(g, tr, idx);
      TrackFX_GetNamedConfigParm(tr, idx, "renamed_name", buf, BUFSIZ);
      std::string str = buf;
      for (auto&& i : safed_fx_names)
      {
        if (str.find(i) != std::string::npos)
        {
          fx_map[g].setSafe(true);
        }
      }
      std::string substr = prefix_string;
      size_t pos = str.find(substr);
      if (pos != std::string::npos)
      {
        possibleOrphans.insert(&fx_map[g]);
      }
    }
  }

  if (shutdown)
  {
    shutdown = false;
    inputTracks.clear();
  }
  // get all paths
  auto paths = findAllPaths(network, inputTracks, outputTracks);

  // get previous state
  GetProjExtState(0, "ak5k", "reallm_sz", buf, BUFSIZ);
  auto state_size = buf[0] != '\0' ? std::stoi(buf) : 0;
  static int allocated_state_size = 0;
  static char* state = new char[allocated_state_size]; // NOLINT
  if (state_size > allocated_state_size)
  {
    delete[] state;               // NOLINT
    state = new char[state_size]; // NOLINT
    allocated_state_size = state_size;
  }
  GetProjExtState(0, "ak5k", "reallm", state, state_size);
  fx_set_prev.clear();
  fx_set_prev = deserializeFxSet(state);

  static std::unordered_set<MediaTrack*> tracks_prev;
  if (!keep_pdc)
  {
    static std::unordered_set<MediaTrack*> tracks;
    tracks.clear();
    static std::unordered_set<MediaTrack*> tracks_diff;
    tracks_diff.clear();
    for (auto&& i : paths)
    {
      for (auto&& j : i)
      {
        tracks.insert(j);
      }
    }

    for (const auto& track : tracks_prev)
    {
      if (tracks.find(track) == tracks.end())
      {
        // The track is not in tracks_prev
        tracks_diff.insert(track);
      }
    }
    tracks_prev = tracks;
    for (auto&& i : tracks)
    {
      TrackFX_SetNamedConfigParm(i, 0, "chain_pdc_mode", "2");
    }
    for (auto&& i : tracks_diff)
    {
      TrackFX_SetNamedConfigParm(i, 0, "chain_pdc_mode", "1");
    }
  }
  // calculate pdc
  static std::unordered_set<TrackFx*> fx_set_to_disable;
  fx_set_to_disable.clear();
  for (auto&& path : paths)
  {
    int accumulating_pdc = 0;
    for (auto&& tr : path)
    {
      accumulating_pdc =
        CalculateTrackPdc(tr, accumulating_pdc, fx_set_to_disable);
    }
  }

  int num_actions = (int)(fx_set_to_disable.size() + fx_set_prev.size()) * 2;
  // disable fx
  PreventUIRefresh(num_actions);
  bool need_undo = false;
  int automation_temp_override = 0;
  for (auto it = fx_set_to_disable.begin(); it != fx_set_to_disable.end();)
  {
    if (fx_set_prev.find(*it) == fx_set_prev.end() && !(*it)->getSafe())
    {
      if (!need_undo)
      {
        Undo_BeginBlock();
        need_undo = true;
        automation_temp_override = GetGlobalAutomationOverride();
        SetGlobalAutomationOverride(6);
      }
      (*it)->disable();
      ++it;
    }
    else
    {
      ++it;
    }
  }
  // enable fx
  for (auto it = fx_set_prev.begin(); it != fx_set_prev.end();)
  {
    if (fx_set_to_disable.find(*it) == fx_set_to_disable.end())
    {
      if (!(*it)->getEnabled())
      {
        if (!need_undo)
        {
          Undo_BeginBlock();
          need_undo = true;
          automation_temp_override = GetGlobalAutomationOverride();
          SetGlobalAutomationOverride(6);
        }
        (*it)->enable();
      }
      if (!(*it)->getSafe())
      {
        it = fx_set_prev.erase(it);
      }
      else
      {
        ++it;
      }
    }
    else
    {
      ++it;
    }
  }

  // update state
  fx_set_to_disable.insert(fx_set_prev.begin(), fx_set_prev.end());

  for (auto&& i : possibleOrphans)
  {
    if (fx_set_to_disable.find(i) == fx_set_to_disable.end())
    {
      if (!need_undo)
      {
        Undo_BeginBlock();
        need_undo = true;
        automation_temp_override = GetGlobalAutomationOverride();
        SetGlobalAutomationOverride(6);
      }
      i->enable();
    }
  }

  if (need_undo)
  {
    SetGlobalAutomationOverride(automation_temp_override);
    Undo_EndBlock("ReaLlm", UNDO_STATE_FX);
  }
  auto state_string = serializeFxSet(fx_set_to_disable);
  if (state_string != std::string(state))
  {
    SetProjExtState(0, "ak5k", "reallm", state_string.c_str());
    SetProjExtState(0, "ak5k", "reallm_sz",
                    std::to_string(state_string.size() + 1).c_str());
  }
  if (shutdown)
  {
    allocated_state_size = 0;
    delete[] state; // NOLINT
  }

  PreventUIRefresh(-num_actions);

  auto end_time = time_precise();
  auto time_diff = end_time - start_time;
  (void)time_diff;
  // ShowConsoleMsg((std::to_string(end_time - start_time) + "\n").c_str());
}

const char* defstring_SetPdcLimit =
  "void\0double\0pdc_factor\0Set pdc limit as factor of audio buffer size.";

void SetPdcLimit(double limit)
{
  pdc_factor = abs(limit);
}

const char* defstring_SetMonitoringFX =
  "void\0bool\0enable\0Set to include MonitoringFX. In REAPER land this means "
  "the fx on the master track record fx chain. Indexed as fx# + 0x1000000, "
  "0-based.";

void SetMonitoringFX(bool enable)
{
  include_monitoring_fx = enable;
}

const char* defstring_SetClearSafe = "void\0bool\0clear_manually_safed_fx\0"
                                     "Set clear safe. Set clear_manually_safed_"
                                     "fx = true to clear manually safed fx";

void SetClearSafe(bool clear_manually_safed_fx)
{
  if (clear_manually_safed_fx)
  {
    for (auto&& i : fx_map)
    {
      i.second.setSafe(false);
    }
    fx_set_prev.clear();
    SetProjExtState(0, "ak5k", "reallm", "");
    SetProjExtState(0, "ak5k", "reallm_sz", "1");
  }
  else
  {
    safed_fx_names.clear();
  }
}

const char* defstring_SetParameterChange =
  "void\0"
  "const char*,int,double,double\0"
  "fx_name,parameter_index,val1,val2\0"
  "Set parameter change. Set val1 = val2 to clear change. Set parameter_index "
  "= -666 to clear all changes. Use this function to set parameter changes "
  "between values val1 and val2 for fx_name and parameter_index instead of "
  "disabling the effect. Use custom fx names to identify individual fx.";

void SetParameterChange(const char* fx_name, int parameter_index, double val1,
                        double val2)
{
  if (val1 == val2)
  {
    auto it = parameter_changes.begin();
    while (it != parameter_changes.end())
    {
      if (it->fx_name == fx_name && it->parameter_index == parameter_index)
      {
        it = parameter_changes.erase(it);
      }
      else
      {
        ++it;
      }
    }
    return;
  }
  if (parameter_index == -666)
  {
    parameter_changes.clear();
    return;
  }
  parameter_changes.emplace_back(
    ParameterChange(fx_name, parameter_index, val1, val2));
}

const char* defstring_SetKeepPdc = "void\0bool\0enable\0Set keep pdc";

void SetKeepPdc(bool enable)
{
  keep_pdc = enable;
}

const char* defstring_GetPaths =
  "void\0bool, MediaTrack*,MediaTrack*,char*,int\0"
  "includeFx,startInOptional,endInOptional,pathStringOut,"
  "pathStringOut_sz"
  "\0"
  "Get paths. Returns a string of the form "
  "\"start:fx#1.fx#2...;track:fxs;...;end:fxs\" where track is the track "
  "number and fx is the fx index. The string is truncated to pathStringOut_sz. "
  "1-based indexing is used. If no MediaTrack* start is provided, all "
  "monitored input tracks are used. If no MediaTrack* end is provided, all "
  "hardware output tracks are used. If includeFx is true, the fx indices are "
  "included.";

void GetPaths(bool includeFx, MediaTrack* startInOptional,
              MediaTrack* endInOptional, char* pathStringOut,
              int pathStringOut_sz)
{
  std::string result;
  std::vector<MediaTrack*> start_points;
  std::vector<MediaTrack*> end_points;
  if (startInOptional == nullptr)
  {
    start_points = inputTracks;
  }
  else
  {
    start_points.push_back(startInOptional);
  }
  if (endInOptional == nullptr)
  {
    end_points = outputTracks;
  }
  else
  {
    end_points.push_back(endInOptional);
  }
  auto paths = findAllPaths(network, start_points, end_points);

  for (auto&& i : paths)
  {
    for (auto&& j : i)
    {
      auto num_track = (int)GetMediaTrackInfo_Value(j, "IP_TRACKNUMBER");
      result += std::to_string(num_track);
      if (includeFx)
      {
        auto fx_count = TrackFX_GetCount(j);
        if (j == GetMasterTrack(NULL) && include_monitoring_fx)
        {
          fx_count = fx_count + TrackFX_GetRecCount(j);
        }
        if (fx_count > 0)
        {
          result += ":";
        }
        for (int k = 0; k < fx_count; k++)
        {
          auto idx = k;
          if (j == GetMasterTrack(NULL) && include_monitoring_fx &&
              idx >= TrackFX_GetCount(j))
          {
            idx = idx - TrackFX_GetCount(j) + 0x1000000;
          }
          char buf[BUFSIZ];
          TrackFX_GetNamedConfigParm(j, idx, "renamed_name", buf, BUFSIZ);
          std::string str = buf;
          std::string prefix = prefix_string;
          if (str.substr(0, prefix.length()) == prefix)
          {
            result += std::to_string(idx + 1);
            result += ".";
          }
        }
        if (fx_count > 0)
        {
          result.pop_back();
        }
      }
      result += ",";
    }
    result.pop_back();
    result += ";";
  }
  if ((int)result.size() > 0)
  {
    result.pop_back();
  }
  if ((int)result.size() > pathStringOut_sz)
  {
    result = result.substr(0, pathStringOut_sz);
  }
  std::copy(result.begin(), result.end(), pathStringOut);
  pathStringOut[result.size()] = '\0'; // don't forget the null terminator
}

const char* defstring_GetSafed =
  "void\0char*,int\0"
  "safeStringOut,safeStringOut_sz"
  "\0"
  "Get safed. Returns a string of the form \"track:fx;track:fx;...\" where "
  "track is the track number and fx is the fx index. The string is truncated "
  "to safeStringOut_sz. 1-based indexing is used. The string is followed by a "
  "| delimited list of fx names that have been set safed.";

void GetSafed(char* safeStringOut, int safeStringOut_sz)
{
  std::string result;
  for (auto&& i : fx_map)
  {
    if (i.second.getSafe())
    {
      if (ValidatePtr2(0, i.second.getTrack(), "MediaTrack*"))
      {
        auto num_track =
          (int)GetMediaTrackInfo_Value(i.second.getTrack(), "IP_TRACKNUMBER");
        result += std::to_string(num_track) + ":" +
                  std::to_string(i.second.getFxIndex() + 1) + ";";
      }
    }
  }
  if ((int)result.size() > 0)
  {
    result.pop_back();
  }
  for (auto&& i : safed_fx_names)
  {
    result += "|";
    result += i;
  }
  if ((int)result.size() > safeStringOut_sz)
  {
    result = result.substr(0, safeStringOut_sz);
  }
  std::copy(result.begin(), result.end(), safeStringOut);
  safeStringOut[result.size()] = '\0'; // don't forget the null terminator
}

const char* defstring_SetSafed =
  "void\0char*,bool\0"
  "fx_name,isSet"
  "\0"
  "Set safed. Set isSet = true to safe fx name. Set isSet = false to unsafe fx "
  "name.";

void SetSafed(const char* fx_name, bool isSet)
{
  if (isSet)
  {
    safed_fx_names.insert(fx_name);
  }
  else
  {
    safed_fx_names.erase(fx_name);
  }
}

void Register()
{
  command_id = plugin_register("custom_action", &action);
  plugin_register("toggleaction", (void*)ToggleActionCallback);
  plugin_register("hookcommand2", (void*)OnAction);

  plugin_register("API_Llm_Do", (void*)main);
  plugin_register("APIdef_Llm_Do", (void*)defstring_Do);
  plugin_register("APIvararg_Llm_Do",
                  reinterpret_cast<void*>(&InvokeReaScriptAPI<&main>));

  plugin_register("API_Llm_SetSafed", (void*)SetSafed);
  plugin_register("APIdef_Llm_SetSafed", (void*)defstring_SetSafed);
  plugin_register("APIvararg_Llm_SetSafed",
                  reinterpret_cast<void*>(&InvokeReaScriptAPI<&SetSafed>));

  plugin_register("API_Llm_GetSafed", (void*)GetSafed);
  plugin_register("APIdef_Llm_GetSafed", (void*)defstring_GetSafed);
  plugin_register("APIvararg_Llm_GetSafed",
                  reinterpret_cast<void*>(&InvokeReaScriptAPI<&GetSafed>));

  plugin_register("API_Llm_GetPaths", (void*)GetPaths);
  plugin_register("APIdef_Llm_GetPaths", (void*)defstring_GetPaths);
  plugin_register("APIvararg_Llm_GetPaths",
                  reinterpret_cast<void*>(&InvokeReaScriptAPI<&GetPaths>));

  plugin_register("API_Llm_SetKeepPdc", (void*)SetKeepPdc);
  plugin_register("APIdef_Llm_SetKeepPdc", (void*)defstring_SetKeepPdc);
  plugin_register("APIvararg_Llm_SetKeepPdc",
                  reinterpret_cast<void*>(&InvokeReaScriptAPI<&SetKeepPdc>));

  plugin_register("API_Llm_SetParameterChange", (void*)SetParameterChange);
  plugin_register("APIdef_Llm_SetParameterChange",
                  (void*)defstring_SetParameterChange);
  plugin_register(
    "APIvararg_Llm_SetParameterChange",
    reinterpret_cast<void*>(&InvokeReaScriptAPI<&SetParameterChange>));

  plugin_register("API_Llm_SetClearSafe", (void*)SetClearSafe);
  plugin_register("APIdef_Llm_SetClearSafe", (void*)defstring_SetClearSafe);
  plugin_register("APIvararg_Llm_SetClearSafe",
                  reinterpret_cast<void*>(&InvokeReaScriptAPI<&SetClearSafe>));

  plugin_register("API_Llm_SetMonitoringFX", (void*)SetMonitoringFX);
  plugin_register("APIdef_Llm_SetMonitoringFX",
                  (void*)defstring_SetMonitoringFX);
  plugin_register(
    "APIvararg_Llm_SetMonitoringFX",
    reinterpret_cast<void*>(&InvokeReaScriptAPI<&SetMonitoringFX>));

  plugin_register("API_Llm_SetPdcLimit", (void*)SetPdcLimit);
  plugin_register("APIdef_Llm_SetPdcLimit", (void*)defstring_SetPdcLimit);
  plugin_register("APIvararg_Llm_SetPdcLimit",
                  reinterpret_cast<void*>(&InvokeReaScriptAPI<&SetPdcLimit>));
}
} // namespace reallm
