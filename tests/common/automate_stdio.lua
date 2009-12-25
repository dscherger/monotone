--
-- data: stdio input data
-- err: expected error code (0, 1 or 2)
-- which: which command to check if data contains input of several command
--        (0 by default)
-- band: the contents of which band to return
--       ('m', the main content, by default)
--
function parse_stdio(data, err, which, band)
  local bands = {}
  local errcodes = {}

  local begin,End,headers = string.find(data, "(.-)\n\n");
  -- FIXME: expand this to a proper header parser if
  -- more headers are added in the future
  check(string.find(headers, "^format%-version: %d$") ~= nil)
  data = string.sub(data, End + 1)

  while true do
    local begin,End,cmdnum,bnd,size = string.find(data, "(%d+):(%l):(%d+):")
    if begin == nil then break end
    cmdnum = cmdnum + 0
    if bands[cmdnum] == nil then
      bands[cmdnum] = { m = "" }
    end
    local content = string.sub(data, End+1, End+size)
    if bnd == "m" then
      bands[cmdnum].m = bands[cmdnum].m .. content
    elseif bnd == "l" then
      errcodes[cmdnum] = tonumber(content)
    else
      if bands[cmdnum][bnd] == nil then
        bands[cmdnum][bnd] = {}
      end
      table.insert(bands[cmdnum][bnd], content)
    end
    data = string.sub(data, End + 1 + size)
  end

  if which == nil then
    which = 0
  end

  check(bands[which] ~= nil)
  check(errcodes[which] ~= nil)

  check(err == errcodes[which])

  if band == nil then
    band = "m"
  end

  check(bands[which][band] ~= nil)

  return bands[which][band]
end

function run_stdio(cmd, err, which, band)
  check(mtn("automate", "stdio"), 0, true, false, cmd)
  return parse_stdio(readfile("stdout"), err, which, band)
end

function run_remote_stdio(server, cmd, err, which, band)
  check(mtn2("automate", "remote_stdio", server.address), 0, true, false, cmd)
  return parse_stdio(readfile("stdout"), err, which, band)
end
