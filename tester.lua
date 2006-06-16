tests = {} -- list of all tests, not visible when running tests
test = {} -- table of per-test values

-- misc global values

-- where the main testsuite file is
srcdir = get_source_dir()
-- where the individual test dirs are
-- most paths will be testdir.."/something"
testdir = srcdir
-- was the -d switch given?
debugging = false

-- combined logfile
logfile = io.open("tester.log", "w")
-- logfiles of failed tests; append these to the main logfile
failed_testlogs = {}

-- This is for redirected output from local implementations
-- of shellutils type stuff (ie, grep).
-- Reason: {set,clear}_redirect don't seem to (always?) work
-- for this (at least on Windows).
files = {stdout = nil, stdin = nil, stderr = nil}


-- misc per-test values
test.root = nil
test.name = nil
test.wanted_fail = false
test.partial_skip = false -- set this to true if you skip part of the test

--probably should put these in the error that gets thrown...
test.errfile = ""
test.errline = -1

-- for tracking background processes
test.bgid = 0
test.bglist = {}

test.log = nil -- logfile for this test


function P(...)
  io.write(unpack(arg))
  io.flush()
  logfile:write(unpack(arg))
end

function L(...)
  test.log:write(unpack(arg))
  test.log:flush()
end

function getsrcline()
  local info
  local depth = 1
  repeat
    depth = depth + 1
    info = debug.getinfo(depth)
  until info == nil
  while src == nil and depth > 1 do
    depth = depth - 1
    info = debug.getinfo(depth)
    if string.find(info.source, "^@.*__driver__%.lua") then
      -- return info.source, info.currentline
      return test.name, info.currentline
    end
  end
end

function locheader()
  local _,line = getsrcline()
  if line == nil then line = -1 end
  if test.name == nil then
    return "\n<unknown>:" .. line .. ": "
  else
    return "\n" .. test.name .. ":" .. line .. ": "
  end
end

function err(what, level)
  if level == nil then level = 2 end
  test.errfile, test.errline = getsrcline()
  local e
  if type(what) == "table" then
    e = what
    if e.bt == nil then e.bt = {} end
    table.insert(e.bt, debug.traceback())
  else
    e = {e = what, bt = {debug.traceback()}}
  end
  error(e, level)
end

do -- replace some builtings with logged versions
  old_mtime = mtime
  mtime = function(name)
    local x = old_mtime(name)
    L(locheader(), "mtime(", name, ") = ", tostring(x), "\n")
    return x
  end

  old_mkdir = mkdir
  mkdir = function(name)
    L(locheader(), "mkdir ", name, "\n")
    old_mkdir(name)
  end

  old_existsonpath = existsonpath
  existsonpath = function(name)
    local r = (old_existsonpath(name) == 0)
    local what
    if r then
      what = "exists"
    else
      what = "does not exist"
    end
    L(locheader(), name, " ", what, " on the path\n")
    return r
  end
end

function numlines(filename)
  local n = 0
  for _ in io.lines(filename) do n = n + 1 end
  L(locheader(), "numlines(", filename, ") = ", n, "\n")
  return n
end

function fsize(filename)
  local file = io.open(filename, "r")
  if file == nil then error("Cannot open file " .. filename, 2) end
  local size = file:seek("end")
  file:close()
  return size
end

function readfile_q(filename)
  local file = io.open(filename, "rb")
  if file == nil then
    error("Cannot open file " .. filename)
  end
  local dat = file:read("*a")
  file:close()
  return dat
end

function readfile(filename)
  L(locheader(), "readfile ", filename, "\n")
  return readfile_q(filename)
end

function readstdfile(filename)
  return readfile(testdir.."/"..filename)
end

function writefile_q(filename, dat)
  local file,e
  if dat == nil then
    file,e = io.open(filename, "a+b")
  else
    file,e = io.open(filename, "wb")
  end
  if file == nil then
    L("Cannot open file ", filename, ": ", e, "\n")
    return false
  end
  if dat ~= nil then
    file:write(dat)
  end
  file:close()
  return true
end

function writefile(filename, dat)
  L(locheader(), "writefile ", filename, "\n")
  return writefile_q(filename, dat)
end

function append(filename, dat)
  L(locheader(), "append to file ", filename, "\n")
  local file,e = io.open(filename, "a+")
  if file == nil then
    L("Cannot open file: ", e, "\n")
    return false
  else
    file:write(dat)
    file:close()
    return true
  end
end

do
  unlogged_copy = copy_recursive
  copy_recursive = nil
  function copy(from, to)
    L(locheader(), "copy ", from, " -> ", to, "\n")
    local ok, res = unlogged_copy(from, to)
    if not ok then
      L(res, "\n")
      return false
    else
      return true
    end
  end
end

do
  local os_rename = os.rename
  os.rename = nil
  os.remove = nil
  function rename(from, to)
    L(locheader(), "rename ", from, " ", to, "\n")
    if exists(to) and not isdir(to) then
      L("Destination ", to, " exists; removing...\n")
      local ok, res = unlogged_remove(to)
      if not ok then
        L("Could not remove ", to, ": ", res, "\n")
        return false
      end
    end
    local ok,res = os_rename(from, to)
    if not ok then
      L(res, "\n")
      return false
    else
      return true
    end
  end
  function unlogged_rename(from, to)
    if exists(to) and not isdir(to) then
      unlogged_remove(to)
    end
    os_rename(from, to)
  end
  unlogged_remove = remove_recursive
  remove_recursive = nil
  function remove(file)
    L(locheader(), "remove ", file, "\n")
    local ok,res = unlogged_remove(file)
    if not ok then
      L(res, "\n")
      return false
    else
      return true
    end
  end
end


function getstd(name, as)
  if as == nil then as = name end
  return copy(testdir .. "/" .. name, as)
end

function get(name, as)
  if as == nil then as = name end
  return getstd(test.name .. "/" .. name, as)
end

-- include from the main tests directory; there's no reason
-- to want to include from the dir for the current test,
-- since in that case it could just go in the driver file.
function include(name)
  local func, e = loadfile(testdir.."/"..name)
  if func == nil then err(e, 2) end
  setfenv(func, getfenv(2))
  func()
end

function trim(str)
  return string.gsub(str, "^%s*(.-)%s*$", "%1")
end

function execute(path, ...)   
   local pid
   local ret = -1
   pid = spawn(path, unpack(arg))
   if (pid ~= -1) then ret, pid = wait(pid) end
   return ret
end

function runcmd(cmd, prefix, bgnd)
  if prefix == nil then prefix = "ts-" end
  if type(cmd) ~= "table" then err("runcmd called with bad argument") end
  local local_redir = cmd.local_redirect
  if cmd.local_redirect == nil then
    if type(cmd[1]) == "function" then
      local_redir = true
    else
      local_redir = false
    end
  end
  if bgnd == true and type(cmd[1]) == "string" then local_redir = false end
  L("\nruncmd: ", tostring(cmd[1]), ", local_redir = ", tostring(local_redir), ", requested = ", tostring(cmd.local_redirect))
  local redir
  if local_redir then
    files.stdin = io.open(prefix.."stdin")
    files.stdout = io.open(prefix.."stdout", "w")
    files.stderr = io.open(prefix.."stderr", "w")
  else
    redir = set_redirect(prefix.."stdin", prefix.."stdout", prefix.."stderr")
  end
  
  local result
  if type(cmd[1]) == "function" then
    L(locheader(), "<function> ")
    for i,x in ipairs(cmd) do
      if i ~= 1 then L(" ", tostring(x)) end
    end
    L("\n")
    result = {pcall(unpack(cmd))}
  elseif type(cmd[1]) == "string" then
    L(locheader())
    for i,x in ipairs(cmd) do
      L(" ", tostring(x))
    end
    L("\n")
    if bgnd then
      result = {pcall(spawn, unpack(cmd))}
    else
      result = {pcall(execute, unpack(cmd))}
    end
  else
    err("runcmd called with bad command table")
  end
  
  if local_redir then
    files.stdin:close()
    files.stdout:close()
    files.stderr:close()
  else
    redir:restore()
  end
  return unpack(result)
end

function samefile(left, right)
  local ldat = nil
  local rdat = nil
  if left == "-" then
    ldat = io.input:read("*a")
    rdat = readfile(right)
  elseif right == "-" then
    rdat = io.input:read("*a")
    ldat = readfile(left)
  else
    if fsize(left) ~= fsize(right) then
      return false
    else
      ldat = readfile(left)
      rdat = readfile(right)
    end
  end
  return ldat == rdat
end

function samelines(f, t)
  local fl = {}
  for l in io.lines(f) do table.insert(fl, l) end
  if not table.getn(fl) == table.getn(t) then
    L(locheader(), string.format("file has %s lines; table has %s\n",
                                 table.getn(fl), table.getn(t)))
    return false
  end
  for i=1,table.getn(t) do
    if fl[i] ~= t[i] then
      L(locheader(), string.format("file[i] = '%s'; table[i] = '%s'\n",
                                   fl[i], t[i]))
      return false
    end
  end
  return true
end

function greplines(f, t)
  local fl = {}
  for l in io.lines(f) do table.insert(fl, l) end
  if not table.getn(fl) == table.getn(t) then
    L(locheader(), string.format("file has %s lines; table has %s\n",
                                 table.getn(fl), table.getn(t)))
    return false
  end
  for i=1,table.getn(t) do
    if not regex.search(t[i], fl[i]) then
      L(locheader(), string.format("file[i] = '%s'; table[i] = '%s'\n",
                                   fl[i], t[i]))
      return false
    end
  end
  return true
end

function grep(...)
  local dogrep = function (flags, what, where)
                   if where == nil and string.sub(flags, 1, 1) ~= "-" then
                     where = what
                     what = flags
                     flags = ""
                   end
                   local quiet = string.find(flags, "q") ~= nil
                   local reverse = string.find(flags, "v") ~= nil
                   if not quiet and files.stdout == nil then err("non-quiet grep not redirected") end
                   local out = 1
                   local infile = files.stdin
                   if where ~= nil then infile = io.open(where) end
                   for line in io.lines(where) do
                     local matched = regex.search(what, line)
                     if reverse then matched = not matched end
                     if matched then
                       if not quiet then files.stdout:write(line, "\n") end
                       out = 0
                     end
                   end
                   if where ~= nil then infile:close() end
                   return out
                 end
  return {dogrep, unpack(arg)}
end

function cat(...)
  local function docat(...)
    local bsize = 8*1024
    for _,x in ipairs(arg) do
      local infile
      if x == "-" then
        infile = files.stdin
      else
        infile = io.open(x, "rb")
      end
      local block = infile:read(bsize)
      while block do
        files.stdout:write(block)
        block = infile:read(bsize)
      end
      if x ~= "-" then
        infile:close()
      end
    end
    return 0
  end
  return {docat, unpack(arg)}
end

function tail(...)
  local function dotail(file, num)
    if num == nil then num = 10 end
    local mylines = {}
    for l in io.lines(file) do
      table.insert(mylines, l)
      if table.getn(mylines) > num then
        table.remove(mylines, 1)
      end
    end
    for _,x in ipairs(mylines) do
      files.stdout:write(x, "\n")
    end
    return 0
  end
  return {dotail, unpack(arg)}
end

function sort(...)
  local function dosort(file)
    local infile
    if file == nil then
      infile = files.stdin
    else
      infile = io.open(file)
    end
    local lines = {}
    for l in infile:lines() do
      table.insert(lines, l)
    end
    if file ~= nil then infile:close() end
    table.sort(lines)
    for _,l in ipairs(lines) do
      files.stdout:write(l, "\n")
    end
    return 0
  end
  return {dosort, unpack(arg)}
end

function log_file_contents(filename)
  L(readfile_q(filename), "\n")
end

function pre_cmd(stdin, ident)
  if ident == nil then ident = "ts-" end
  if stdin == true then
    unlogged_copy("stdin", ident .. "stdin")
  elseif type(stdin) == "table" then
    unlogged_copy(stdin[1], ident .. "stdin")
  else
    local infile = io.open(ident .. "stdin", "w")
    if stdin ~= nil and stdin ~= false then
      infile:write(stdin)
    end
    infile:close()
  end
  L("stdin:\n")
  log_file_contents(ident .. "stdin")
end

function post_cmd(result, ret, stdout, stderr, ident)
  if ret == nil then ret = 0 end
  if ident == nil then ident = "ts-" end
  L("stdout:\n")
  log_file_contents(ident .. "stdout")
  L("stderr:\n")
  log_file_contents(ident .. "stderr")
  if result ~= ret and ret ~= false then
    err("Check failed (return value): wanted " .. ret .. " got " .. result, 3)
  end

  if stdout == nil then
    if fsize(ident .. "stdout") ~= 0 then
      err("Check failed (stdout): not empty", 3)
    end
  elseif type(stdout) == "string" then
    local realout = io.open(ident .. "stdout")
    local contents = realout:read("*a")
    realout:close()
    if contents ~= stdout then
      err("Check failed (stdout): doesn't match", 3)
    end
  elseif type(stdout) == "table" then
    if not samefile(ident .. "stdout", stdout[1]) then
      err("Check failed (stdout): doesn't match", 3)
    end
  elseif stdout == true then
    unlogged_remove("stdout")
    unlogged_rename(ident .. "stdout", "stdout")
  end

  if stderr == nil then
    if fsize(ident .. "stderr") ~= 0 then
      err("Check failed (stderr): not empty", 3)
    end
  elseif type(stderr) == "string" then
    local realerr = io.open(ident .. "stderr")
    local contents = realerr:read("*a")
    realerr:close()
    if contents ~= stderr then
      err("Check failed (stderr): doesn't match", 3)
    end
  elseif type(stderr) == "table" then
    if not samefile(ident .. "stderr", stderr[1]) then
      err("Check failed (stderr): doesn't match", 3)
    end
  elseif stderr == true then
    unlogged_remove("stderr")
    unlogged_rename(ident .. "stderr", "stderr")
  end
end

-- std{out,err} can be:
--   * false: ignore
--   * true: ignore, copy to stdout
--   * string: check that it matches the contents
--   * nil: must be empty
--   * {string}: check that it matches the named file
-- stdin can be:
--   * true: use existing "stdin" file
--   * nil, false: empty input
--   * string: contents of string
--   * {string}: contents of the named file

function bg(torun, ret, stdout, stderr, stdin)
  test.bgid = test.bgid + 1
  local out = {}
  out.prefix = "ts-" .. test.bgid .. "-"
  pre_cmd(stdin, out.prefix)
  L("Starting background command...")
  local ok,pid = runcmd(torun, out.prefix, true)
  if not ok then err(pid, 2) end
  if pid == -1 then err("Failed to start background process\n", 2) end
  out.pid = pid
  test.bglist[test.bgid] = out
  out.id = test.bgid
  out.retval = nil
  out.locstr = locheader()
  out.cmd = torun
  out.expret = ret
  out.expout = stdout
  out.experr = stderr
  local mt = {}
  mt.__index = mt
  mt.finish = function(obj, timeout)
                if obj.retval ~= nil then return end
                
                if timeout == nil then timeout = 0 end
                if type(timeout) ~= "number" then
                  err("Bad timeout of type "..type(timeout))
                end
                local res
                obj.retval, res = timed_wait(obj.pid, timeout)
                if (res == -1) then
                  kill(obj.pid, 15) -- TERM
                  obj.retval, res = timed_wait(obj.pid, 2)
                  if (res == -1) then
                    kill(obj.pid, 9) -- KILL
                    obj.retval, res = timed_wait(obj.pid, 2)
                  end
                end
                
                test.bglist[obj.id] = nil
                L(locheader(), "checking background command from ", out.locstr,
                  table.concat(out.cmd, " "), "\n")
                post_cmd(obj.retval, out.expret, out.expout, out.experr, obj.prefix)
                return true
              end
  mt.wait = function(obj, timeout)
              if obj.retval ~= nil then return end
              if timeout == nil then
                obj.retval = wait(obj.pid)
              else
                local res
                obj.retval, res = timed_wait(obj.pid, timeout)
                if res == -1 then
                  obj.retval = nil
                  return false
                end
              end
              test.bglist[obj.id] = nil
              L(locheader(), "checking background command from ", out.locstr,
                table.concat(out.cmd, " "), "\n")
              post_cmd(obj.retval, out.expret, out.expout, out.experr, obj.prefix)
              return true
            end
  return setmetatable(out, mt)
end

function runcheck(cmd, ret, stdout, stderr, stdin)
  if ret == nil then ret = 0 end
  pre_cmd(stdin)
  local ok, result = runcmd(cmd)
  if ok == false then
    err(result, 2)
  end
  post_cmd(result, ret, stdout, stderr)
  return result
end

function indir(dir, what)
  if type(what) ~= "table" then
    err("bad argument of type "..type(what).." to indir()")
  end
  local function do_indir()
    local savedir = chdir(dir)
    if savedir == nil then
      err("Cannot chdir to "..dir)
    end
    local ok, res
    if type(what[1]) == "function" then
      ok, res = pcall(unpack(what))
    elseif type(what[1]) == "string" then
      ok, res = pcall(execute, unpack(what))
    else
      err("bad argument to indir(): cannot execute a "..type(what[1]))
    end
    chdir(savedir)
    if not ok then err(res) end
    return res
  end
  return {do_indir, local_redirect = (type(what[1]) == "function")}
end

function check(first, ...)
  if type(first) == "table" then
    return runcheck(first, unpack(arg))
  elseif type(first) == "boolean" then
    if not first then err("Check failed: false", 2) end
  elseif type(first) == "number" then
    if first ~= 0 then
      err("Check failed: " .. first .. " ~= 0", 2)
    end
  else
    err("Bad argument to check() (" .. type(first) .. ")", 2)
  end
  return first
end

function skip_if(chk)
  if chk then
    err(true, 2)
  end
end

function xfail_if(chk, ...)
  local ok,res = pcall(check, unpack(arg))
  if ok == false then
    if chk then err(false, 2) else err(err, 2) end
  else
    if chk then
      test.wanted_fail = true
      L("UNEXPECTED SUCCESS\n")
    end
  end
end

function log_error(e)
  if type(e) == "table" then
    L("\n", tostring(e.e), "\n")
    for i,bt in ipairs(e.bt) do
      if i ~= 1 then L("Rethrown from:") end
      L(bt)
    end
  else
    L("\n", tostring(e), "\n")
  end
end

function run_tests(args)
  local torun = {}
  local run_all = true
  local list_only = false
  for i,a in pairs(args) do
    local _1,_2,l,r = string.find(a, "^(-?%d+)%.%.(-?%d+)$")
    if _1 then
      l = l + 0
      r = r + 0
      if l < 1 then l = table.getn(tests) + l + 1 end
      if r < 1 then r = table.getn(tests) + r + 1 end
      if l > r then l,r = r,l end
      for j = l,r do
        torun[j]=j
      end
      run_all = false
    elseif string.find(a, "^-?%d+$") then
      r = a + 0
      if r < 1 then r = table.getn(tests) + r + 1 end
      torun[r] = r
      run_all = false
    elseif a == "-d" then
      debugging = true
    elseif a == "-l" then
      list_only = true
    else
      -- pattern
      local matched = false
      for i,t in pairs(tests) do
        if regex.search(a, t) then
          torun[i] = i
          matched = true
        end
      end
      if matched then
        run_all = false
      else
        print(string.format("Warning: pattern '%s' does not match any tests.", a))
      end
    end
  end
  if not list_only then
    logfile:write("Running on ", get_ostype(), "\n\n")
    P("Running tests...\n")
  end
  local counts = {}
  counts.success = 0
  counts.skip = 0
  counts.xfail = 0
  counts.noxfail = 0
  counts.fail = 0
  counts.total = 0
  counts.of_interest = 0
  local of_interest = {}

  local function runtest(i, tname)
    local env = {}
    local genv = getfenv(0)
    for x,y in pairs(genv) do
      env[x] = y
      -- we want changes to globals in a test to be visible to
      -- already-defined functions
      if type(y) == "function" then
        pcall(setfenv, y, env)
      end
    end
    env.tests = nil -- don't let them mess with this
    
    test.bgid = 0
    test.name = tname
    test.wanted_fail = false
    test.partial_skip = false
    local shortname = nil
    test.root, shortname = go_to_test_dir(tname)
    test.errfile = ""
    test.errline = -1
    test.bglist = {}
    
    local test_header = ""
    if i < 100 then test_header = test_header .. " " end
    if i < 10 then test_header = test_header .. " " end
    test_header = test_header .. i .. " " .. shortname .. " "
    local spacelen = 45 - string.len(shortname)
    local spaces = string.rep(" ", 50)
    if spacelen > 0 then
      test_header = test_header .. string.sub(spaces, 1, spacelen)
    end
    P(test_header)

    local tlog = test.root .. "/tester.log"
    test.log = io.open(tlog, "w")
    L("Test number ", i, ", ", shortname, "\n")

    local driverfile = testdir .. "/" .. test.name .. "/__driver__.lua"
    local driver, e = loadfile(driverfile)
    local r
    if driver == nil then
      r = false
      e = "Could not load driver file " .. driverfile .. " .\n" .. e
    else
      setfenv(driver, env)
      local oldmask = posix_umask(0)
      posix_umask(oldmask)
      r,e = xpcall(driver, debug.traceback)
      local errline = test.errline
      for i,b in pairs(test.bglist) do
        local a,x = pcall(function () b:finish(0) end)
        if r and not a then
          r = a
          e = x
        elseif not a then
          L("Error cleaning up background processes: ", tostring(b.locstr), "\n")
        end
      end
      if type(env.cleanup) == "function" then
        local a,b = pcall(env.cleanup)
        if r and not a then
          r = a
          e = b
        end
      end
      test.errline = errline
      restore_env()
      posix_umask(oldmask)
    end
    
    -- set our functions back to the proper environment
    local genv = getfenv(0)
    for x,y in pairs(genv) do
      if type(y) == "function" then
        pcall(setfenv, y, genv)
      end
    end
    
    if r then
      if test.wanted_fail then
        P("unexpected success\n")
        test.log:close()
        leave_test_dir()
        counts.noxfail = counts.noxfail + 1
        counts.of_interest = counts.of_interest + 1
        table.insert(of_interest, test_header .. "unexpected success")
      else
        if test.partial_skip then
          P("partial skip\n")
        else
          P("ok\n")
        end
        test.log:close()
        if not debugging then clean_test_dir(tname) end
        counts.success = counts.success + 1
      end
    else
      if test.errline == nil then test.errline = -1 end
      if type(e) ~= "table" then
        local tbl = {e = e, bt = {"no backtrace; type(err) = "..type(e)}}
        e = tbl
      end
      if e.e == true then
        P(string.format("skipped (line %i)\n", test.errline))
        test.log:close()
        if not debugging then clean_test_dir(tname) end
        counts.skip = counts.skip + 1
      elseif e.e == false then
        P(string.format("expected failure (line %i)\n", test.errline))
        test.log:close()
        leave_test_dir()
        counts.xfail = counts.xfail + 1
      else
        result = string.format("FAIL (line %i)", test.errline)
        P(result, "\n")
        log_error(e)
        table.insert(failed_testlogs, tlog)
        test.log:close()
        leave_test_dir()
        counts.fail = counts.fail + 1
        counts.of_interest = counts.of_interest + 1
        table.insert(of_interest, test_header .. result)
      end
    end
    counts.total = counts.total + 1
  end

  save_env()
  if run_all then
    for i,t in pairs(tests) do
      if list_only then
        if i < 10 then P(" ") end
        if i < 100 then P(" ") end
        P(i .. " " .. t .. "\n")
      else
        runtest(i, t)
      end
    end
  else
    for i,t in pairs(tests) do
      if torun[i] == i then
        if list_only then
          if i < 10 then P(" ") end
          if i < 100 then P(" ") end
          P(i .. " " .. t .. "\n")
        else
          runtest(i, t)
        end
      end
    end
  end
  
  if list_only then
    logfile:close()
    return 0
  end
  
  if counts.of_interest ~= 0 and (counts.total / counts.of_interest) > 4 then
   P("\nInteresting tests:\n")
   for i,x in ipairs(of_interest) do
     P(x, "\n")
   end
  end
  P("\n")
  P(string.format("Of %i tests run:\n", counts.total))
  P(string.format("\t%i succeeded\n", counts.success))
  P(string.format("\t%i failed\n", counts.fail))
  P(string.format("\t%i had expected failures\n", counts.xfail))
  P(string.format("\t%i succeeded unexpectedly\n", counts.noxfail))
  P(string.format("\t%i were skipped\n", counts.skip))

  for i,log in pairs(failed_testlogs) do
    local tlog = io.open(log, "r")
    if tlog ~= nil then
      local dat = tlog:read("*a")
      tlog:close()
      logfile:write("\n", string.rep("*", 50), "\n")
      logfile:write(dat)
    end
  end
  logfile:close()

  if counts.success + counts.skip + counts.xfail == counts.total then
    return 0
  else
    return 1
  end
end
