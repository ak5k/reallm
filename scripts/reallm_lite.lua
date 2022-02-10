--example of how to run ReaLlm every 30th defer cycle.

if not reaper.APIExists("Llm_Do") then
  reaper.ShowMessageBox("ReaLlm not installed?", "ReaLlm Lite", 0)
  return
end

local function ToggleCommandState(state)
  local _, _, sec, cmd = reaper.get_action_context()
  reaper.SetToggleCommandState(sec, cmd, state)
  reaper.RefreshToolbar2(sec, cmd)
end

local count
function main(exit)
  count = count or 0
  count = count + 1
  if count > 30 or exit then
    count = 0
    reaper.Llm_Do(exit)
  end
  reaper.defer(main)
end

ToggleCommandState(1)
reaper.defer(main)

function exit()
  local exit = true
  reaper.Llm_Do(exit)
  ToggleCommandState(0)
end

reaper.atexit(exit)
