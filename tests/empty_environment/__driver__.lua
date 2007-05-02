
skip_if(not existsonpath("env"))
mtn_setup()

function noenv_mtn(...)
  return {"env", "-i", unpack(mtn(unpack(arg)))}
end

if ostype == "Windows" then
  local iconv = getpathof("libiconv-2", ".dll")
  local zlib = getpathof("zlib1", ".dll")
  copy(iconv, "libiconv-2.dll")
  copy(zlib, "zlib1.dll")
elseif string.sub(ostype, 1, 6) == "CYGWIN" then
  local cygwin = getpathof("cygwin1", ".dll")
  local iconv = getpathof("cygiconv-2", ".dll")
  local intl = getpathof("cygintl-3", ".dll")
  local intl8 = getpathof("cygintl-8", ".dll")
  local zlib = getpathof("cygz", ".dll")
  local boostfile = getpathof("cygboost_filesystem-gcc-mt-1_33_1", ".dll")
  local boostregex = getpathof("cygboost_regex-gcc-mt-1_33_1", ".dll")
  copy(cygwin, "cygwin1.dll")
  copy(iconv, "cygiconv-2.dll")
  copy(intl, "cygintl-3.dll")
  copy(intl8, "cygintl-8.dll")
  copy(zlib, "cygz.dll")
  copy(boostfile, "cygboost_filesystem-gcc-mt-1_33_1.dll")
  copy(boostregex, "cygboost_regex-gcc-mt-1_33_1.dll")
end

check(noenv_mtn("--help"), 0, false, false)
writefile("testfile", "blah blah")
check(noenv_mtn("add", "testfile"), 0, false, false)
check(noenv_mtn("commit", "--message=foo"), 0, false, false)
