
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
            unpack(mtn(unpack(arg)))}
  else
    return {"env", "-i",
            unpack(mtn(unpack(arg)))}
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
  local cygwin = getpathof("cygwin1", ".dll")
  local iconv = getpathof("cygiconv-2", ".dll")
  local intl = getpathof("cygintl-3", ".dll")
  local intl8 = getpathof("cygintl-8", ".dll")
  local zlib = getpathof("cygz", ".dll")
  copy(cygwin, "cygwin1.dll")
  copy(iconv, "cygiconv-2.dll")
  copy(intl, "cygintl-3.dll")
  copy(intl8, "cygintl-8.dll")
  copy(zlib, "cygz.dll")
end

check(noenv_mtn("--help"), 0, false, false)
writefile("testfile", "blah blah")
check(noenv_mtn("add", "testfile"), 0, false, false)
check(noenv_mtn("commit", "--branch=testbranch", "--message=foo"), 0, false, false)
