function run_stdio(cmd, err, which, band)
  check(mtn("automate", "stdio"), 0, true, false, cmd)

  local parse_stdio = function(dat, which)
    local bands = {}
    local errcodes = {}
    while true do
      local begin,End,cmdnum,res,band,size = string.find(dat, "(%d+):(%d+):(%l):(%d+):")
      if begin == nil then break end
      cmdnum = cmdnum + 0
      if bands[cmdnum] == nil then
        bands[cmdnum] = { m = "" }
      end
      local content = string.sub(dat, End+1, End+size)
      if band == "m" or band == "l" then
        bands[cmdnum].m = bands[cmdnum].m .. content
      else
        if bands[cmdnum][band] == nil then
          bands[cmdnum][band] = {}
        end
        table.insert(bands[cmdnum][band], content)
      end
      dat = string.sub(dat, End + 1 + size)
      errcodes[cmdnum] = tonumber(res)
    end
    return bands[which], errcodes[which]
  end

  if which == nil then
    which = 0
  end

  local bands,errcode = parse_stdio(readfile("stdout"), which)
  check(err == errcode)
  if band == nil then
    band = "m"
  end
  return bands[band]
end
