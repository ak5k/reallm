-- example of how to run ReaLlm every 30th defer cycle.
local defer_count = 0

if not reaper.APIExists("Llm_Do") then
    reaper.ShowMessageBox("ReaLlm extension not installed?", "ReaLlm Lite", 0)
    return
end

local function ToggleCommandState(state)
    local _, _, sec, cmd = reaper.get_action_context()
    reaper.SetToggleCommandState(sec, cmd, state)
    reaper.RefreshToolbar2(sec, cmd)
end

reaper.Llm_Set("P_PDCMODECHECK", "1")

local count
local function main(exit)
    count = count or 0
    count = count + 1
    if count > defer_count or exit then
        count = 0
        local time0 = reaper.time_precise()
        reaper.Llm_Do(exit)
        local time1 = reaper.time_precise()
        reaper.ShowConsoleMsg(time1 - time0 .. "\n")
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
