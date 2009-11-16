--
--  remote_export.lua -- Monotone Lua extension command "mtn remote_export"
--  Copyright (C) 2009 Thomas Keller <me@thomaskeller.biz>
--
--  This program is made available under the GNU GPL version 2.0 or
--  greater. See the accompanying file COPYING for details.
--
--  This program is distributed WITHOUT ANY WARRANTY; without even the
--  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
--  PURPOSE.
--

register_command(
    "remote_export", "HOST[:PORT] SELECTOR [DIRECTORY]",
    "Exports a remote revision locally",
    "Exports the remote revision given by SELECTOR locally into "..
    "DIRECTORY. If no DIRECTORY is given, the branch name is used "..
    "as directory name.",
    "command_remote_export"
)

alias_command(
    "remote_export",
    "re"
)

function command_remote_export(host, selector, directory)
    --  argument sanity checking
    if host == nil or selector == nil then
        io.stderr:write("mtn: remote_export: missing arguments - "..
            "see 'mtn help remote_export'\n")
        return
    end

    -- we need 'automate remote' which is available as of mtn 0.46
    local res, version = mtn_automate("interface_version")
    if not res then
        io.stderr:write("mtn: remote_export: could not determine interface "..
                        "version of this version of monotone\n")
        return
    end

    version = tonumber(string.sub(version, 0, -4))
    if version < 12 then
        io.stderr:write("mtn: remote_export: this command needs a "..
                        "monotone with an automate interface version "..
                        "of 12.0 or higher (monotone 0.46 or higher)")
        return
    end

    local remotecall = function(host, ...)
        return mtn_automate("remote",
                            "--quiet",
                            "--remote-stdio-host="..host,
                            "--",
                            unpack(arg))
    end

    local splitlines = function(str)
        local t = {}
        local helper = function(line)
            if string.len(line) > 0 then
                table.insert(t, line)
            end
            return ""
        end
        helper((str:gsub("(.-)\r?\n", helper)))
        return t
    end

    local res, revs = remotecall(host, "select", selector)
    if not res then
        io.stderr:write("mtn: remote_export: could not resolve selector\n")
        return
    end

    revs = splitlines(revs)
    if table.maxn(revs) == 0 then
        io.stderr:write("mtn: remote_export: selector '"..selector.."' "..
                        "matches no revisions remotely\n")
        return
    end

    if table.maxn(revs) > 1 then
        io.stderr:write("mtn: remote_export: selector '"..selector.."' "..
                        "expands to more than one revision, please pick one:\n")
        for _,rev in ipairs(revs) do
            io.stderr:write("mtn: remote_export:   "..rev.."\n")
        end
        return
    end

    local rev = revs[1]

    if directory == nil then
        local res, str = remotecall(host, "certs", rev)
        if  not res then
            io.stderr:write("mtn: remote_export: could not query "..
                            "certs of "..rev.."\n")
            return
        end
        local basicio = parse_basic_io(str)
        local nextIsBranch = false
        for _,line in ipairs(basicio) do
            if line.name == "name" and line.values[1] == "branch" then
                nextIsBranch = true
            end

            if line.name == "value" and nextIsBranch then
                directory = line.values[1]
                break
            end
        end
    end

    if string.len(directory) == 0 then
        io.stderr:write("mtn: remote_export: invalid directory given\n")
        return
    end

    local rc = execute("mkdir", directory)
    if rc ~= 0 then
        io.stderr:write("mtn: remote_export: could not create directory '"..
                        directory.."'\n")
        return
    end

    local res, str = remotecall(host, "get_manifest_of", rev)
    if  not res then
        io.stderr:write("mtn: remote_export: could not query "..
                        "manifest of "..rev.."\n")
        return
    end

    local basicio = parse_basic_io(str)
    local dirs = {}
    local files = {}

    local filePath = nil
    for i,line in ipairs(basicio) do
        if line.name == "dir" and string.len(line.values[1]) > 0 then
            table.insert(dirs, line.values[1])
        end

        if line.name == "file" then
            filePath = line.values[1]
        end

        if line.name == "content" and filePath ~= nil then
            local executable = false
            if table.maxn(basicio) >= i+1 and
                basicio[i+1].name == "attr" and
                basicio[i+1].values[1] == "mtn:execute" and
                basicio[i+1].values[2] == "true" then
                executable = true
            end

            table.insert(files, {
                path = filePath,
                hash = line.values[1],
                exec = executable
            })
            filePath = nil
        end
    end

    io.stderr:write("mtn: remote_export: exporting '"..rev..
                    " ("..table.maxn(files).." files, "..table.maxn(dirs)..
                    " directories) to '"..directory.."'\n")

    for i,dir in ipairs(dirs) do
        local localPath = directory.. "/" .. dir;
        local rc = execute("mkdir", localPath)
        if rc ~= 0 then
            io.stderr:write("mtn: remote_export: could not create directory '"..
                            localPath.."\n")
            return
        end

        io.stderr:write("mtn: remote_export: ["..i.."/"..table.maxn(dirs).."] "
                        ..dir.." created\n")
    end

    for i,file in ipairs(files) do
        local res, contents = remotecall(host, "get_file", file.hash)
        if not res then
           io.stderr:write("mtn: remote_export: could not query contents of "..
                           "file '"..file.path.."' (hash: "..file.hash..")\n");
           return
        end

        local localPath = directory .. "/" .. file.path

        fp = io.open(localPath, "wb")
        fp:write(contents)
        fp:close()

        io.stderr:write("mtn: remote_export: ["..i.."/"..table.maxn(files).."] "
                        .. file.path .." fetched and saved\n")

        if file.exec then
            local rc = execute("chmod", "+x", localPath)
            if rc ~= 0 then
                io.stderr:write("mtn: remote_export: could not give '"..
                                localPath .."' executable privileges\n")
                return
            end

            io.stderr:write("mtn: remote_export: "..
                            "["..i.."/"..table.maxn(files).."] executable bit "..
                            "set on "..file.path.."\n")
        end
    end
end

