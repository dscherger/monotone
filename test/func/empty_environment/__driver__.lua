
skip_if(not existsonpath("env"))
mtn_setup()

function noenv_mtn(...)
  -- strip all environment variables, except for the library path, so that
  -- we can link against libraries in non-standard locations. So far I've
  -- only tested that on Linux.
  save_LD_LIBRARY_PATH = os.getenv("LD_LIBRARY_PATH")
  if save_LD_LIBRARY_PATH then
    return {"env", "-i",
            "LD_LIBRARY_PATH="..save_LD_LIBRARY_PATH,
            unpack(mtn(...))}
  else
    return {"env", "-i",
            unpack(mtn(...))}
  end
end

if ostype == "Windows" then
  -- This used Depends.exe
  -- It comes with Visual Studio, and can also be downloaded from
  -- http://www.dependencywalker.com/
  -- If you install it separately, make sure to put it in your %PATH%.
  -- If you have Visual Studio, it should be detected automatically from its
  -- association with .dwi files.
  local depends = ""
  if existsonpath("Depends.exe")
  then
    depends = "Depends.exe"
  else
    check({"reg", "query", "hklm\\software\\classes\\dwifile\\shell\\open\\command", "/ve"}, 0, true, false)
    if qgrep("Depends.exe", "stdout")
    then
      local data = readfile("stdout")
      depends = string.match(data, "REG_SZ%s*([^%s].*Depends.exe)")
    end
  end
  check(depends ~= "" and type(depends) == "string")
  
  -- don't care about the exit code
  check({depends, "/c", "/oc:dep.csv", "-f:1", monotone_path}, false)
  local file = open_or_err("dep.csv")
  for line in file:lines()
  do
    local name = line:match('^[^,]*,"([^"]*)",%d%d%d%d%-')
    if name == nil
    then
      L("No file found in line: " .. line .. "\n")
    elseif name:match("\\[Ww][Ii][Nn][Dd][Oo][Ww][Ss]\\") ~= nil
    then
      L("Skipping '" .. name .. "'\n")
    else
      L("Copying '" .. name .. "'\n")
      local base = name:match("([^\\/]*)$")
      copy(name, base)
    end
  end
  file:close()
elseif string.sub(ostype, 1, 6) == "CYGWIN" then
  for _,name in pairs({
                        "cyggcc_s-1",
                        "cygiconv-2",
                        "cygidn-11",
                        "cygintl-8",
                        "cyglua-5.1",
                        "cygpcre-0",
                        "cygsqlite3-0",
                        "cygstdc++-6",
                        "cygwin1",
                        "cygz",
		      }) do
    local file = getpathof(name, ".dll")
    copy(file, name..".dll");
  end
end

check(noenv_mtn("--help"), 0, false, false)
writefile("testfile", "blah blah")
check(noenv_mtn("add", "testfile"), 0, false, false)
check(noenv_mtn("commit", "--branch=testbranch", "--message=foo"), 0, false, false)
