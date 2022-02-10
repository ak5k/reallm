--example of how to run ReaLlm every 30th defer cycle.
local defer_count = 30


if not reaper.APIExists("Llm_Do") then
  reaper.ShowMessageBox("ReaLlm not installed?", "ReaLlm Lite", 0)
  return
end

local function ToggleCommandState(state)
  local _, _, sec, cmd = reaper.get_action_context()
  reaper.SetToggleCommandState(sec, cmd, state)
  reaper.RefreshToolbar2(sec, cmd)
end

reaper.Llm_Set("P_PDCMODECHECK", "0")

local count
local function main(exit)
  count = count or 0
  count = count + 1
  if count > defer_count or exit then
    count = 0
    reaper.Llm_Do(exit)
  end
  reaper.defer(main)
end

ToggleCommandState(1)
reaper.defer(main)

local function exit()
  local exit = true
  reaper.Llm_Do(exit)
  ToggleCommandState(0)
end

reaper.atexit(exit)
