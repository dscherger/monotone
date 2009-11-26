include("/common/automate_stdio.lua")

mtn_setup()

-- check informational messages, warnings and errors
out = run_stdio("l8:bandtest4:infoe", 0, 0, "p")
check(type(out) == "table" and table.maxn(out) == 1)

out = run_stdio("l8:bandtest7:warninge", 0, 0, "w")
check(type(out) == "table" and table.maxn(out) == 1)

out = run_stdio("l8:bandtest5:errore", 2, 0, "e")
check(type(out) == "table" and table.maxn(out) == 1)

-- check tickers
tickers = run_stdio("l8:bandtest6:tickere", 0, 0, "t")
check(type(out) == "table")

function split(str, pat)
    local t = {}  -- NOTE: use {n = 0} in Lua-5.0
    local fpat = "(.-)" .. pat
    local last_end = 1
    local s, e, cap = str:find(fpat, 1)
    while s do
        if s ~= 1 or cap ~= "" then
            table.insert(t,cap)
        end
        last_end = e+1
        s, e, cap = str:find(fpat, last_end)
    end
    if last_end <= #str then
        cap = str:sub(last_end)
        table.insert(t, cap)
    end
    return t
end

ticker_data = {}
for _,tick in ipairs(tickers) do
    ticks = split(tick, ";")
    check(table.maxn(ticks) > 0)
    for _,mtick in ipairs(ticks) do
        if string.len(mtick) > 0 then
            local begin,End,short,ticktype,content =
                string.find(mtick, "([%l%u%d]+)([=:#])(.+)")
            if begin == nil then
               short = mtick
            end
            if ticker_data[short] == nil then
                -- check the ticker's name definition
                check(ticktype == ":")
                ticker_data[short] = {}
                check(string.len(content) > 0)
            else
                check(ticktype ~= ":")
                -- check and remember the ticker's total value (if any)
                if ticktype == "=" then
                    check(ticker_data[short].total == nil)
                    ticker_data[short].total = tonumber(content)
                    check(ticker_data[short].total ~= nil)
                -- check and remember the ticker's progress
                elseif ticktype == "#" then
                    progress = tonumber(content)
                    check(progress ~= nil)
                    check(ticker_data[short].total == 0 or
                            progress <= ticker_data[short].total)
                    ticker_data[short].progress = progress
                -- check for the ticker's end and remove it
                elseif ticktype == nil then
                    check(ticker_data[short] ~= nil)
                    check(ticker_data[short].total == 0 or
                            ticker_data[short].progress == ticker_data[short].total)
                    ticker_data[short] = nil
                end
            end
        end
    end
end

-- finally check if all tickers are completed
check(table.maxn(ticker_data) == 0)


