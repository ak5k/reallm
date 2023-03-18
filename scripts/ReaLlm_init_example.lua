-- ReaLlm init example script
reallmID = reaper.NamedCommandLookup("_AK5K_REALLM")
if reallmID == 0 then return end
state = reaper.GetToggleCommandState(reallmID)

-- set PDC latency limit 0.9 times current audio device buffer size
reaper.Llm_Set("P_PDCLIMIT", "0.9")

-- also handle monitoring fx
reaper.Llm_Set("P_MONITORINGFX", "yes")

--[[ 
Changes 'VST3: Pro-C 2 (FabFilter)' plugin parameter index 8 (Lookahead) on/off
when instances are found in monitored signalchain.
]] --
reaper.Llm_set("P_PARAMCHANGE", "VST3: Pro-C 2 (FabFilter),8,0,1")

-- enable / disable ReaLlm
reaper.Main_OnCommand(reallmID, 0)

-- update state of this script
_, _, sectionID, cmdID = reaper.get_action_context()
reaper.SetToggleCommandState(sectionID, cmdID, ((state < 1) and 1 or 0))
reaper.RefreshToolbar2(sectionID, cmdID);
