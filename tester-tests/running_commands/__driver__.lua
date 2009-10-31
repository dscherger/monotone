-- verify that we can execute programs with their stdin
-- and stdout redirected from files

get("script")

if existsonpath("sh") then
   check({"sh"}, 0, true, true, {"script"})
elseif existsonpath("cmd") then
   check({"cmd"}, 0, true, true, {"script"})
else
   check(false)
end

data = readfile("stdout")
check(string.find(data, "fnord") ~= nil)
