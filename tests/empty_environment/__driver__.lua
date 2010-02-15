
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
  local iconv = getpathof("libiconv-2", ".dll")
  local intl = getpathof("libintl-8", ".dll")
  local zlib = getpathof("zlib1", ".dll")
  copy(iconv, "libiconv-2.dll")
  copy(intl, "libintl-8.dll")
  copy(zlib, "zlib1.dll")
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
