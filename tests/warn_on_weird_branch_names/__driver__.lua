
mtn_setup()

addfile("foo", "foofile")
commit()
R=base_revision()

local branch_map = {
    ["com.project"]             = 0,
    ["com.project.release-1.0"] = 0,
    ["-com.project"]            = 1,
    ["com.project*"]            = 1,
    ["com.pr?ject"]             = 1,
    ["com.pr[o]j{e}ct"]         = 1,
    ["com.project-%20"]         = 1,
    ["com.project-1,0"]         = 1,
    ["^com.project!"]           = 1
}

for branch,res in pairs(branch_map) do
    check(mtn("cert", "--", R, "branch", branch), 0, false, true)
    if res == 1 then
        check(qgrep("contains meta characters", "stderr"))
    else
        check(not qgrep("contains meta characters", "stderr"))
    end
end
