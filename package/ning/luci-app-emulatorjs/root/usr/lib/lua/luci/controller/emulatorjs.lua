module("luci.controller.emulatorjs", package.seeall)

function index()
    entry({"admin", "services", "emulatorjs"}, template("emulatorjs/index"), _("游戏厅"), 90).dependent = true
    entry({"admin", "services", "emulatorjs", "upload"}, call("action_upload"), nil).leaf = true
    entry({"admin", "services", "emulatorjs", "delete"}, call("action_delete"), nil).leaf = true
end

function action_upload()
    local http = require "luci.http"
    local system = http.formvalue("system")
    
    if not (system == "nes" or system == "snes" or system == "gba" or system == "arcade") then
        http.status(400, "Invalid system category")
        return
    end
    
    local upload_dir = "/www/emulatorjs/roms/" .. system
    local fp
    
    http.setfilehandler(
        function(meta, chunk, eof)
            if not fp and meta and meta.file then
                -- Sanitize filename to prevent path traversal
                local filename = meta.file:gsub("[/\\]", "")
                fp = io.open(upload_dir .. "/" .. filename, "wb")
            end
            if chunk and fp then
                fp:write(chunk)
            end
            if eof and fp then
                fp:close()
            end
        end
    )
    
    http.redirect(luci.dispatcher.build_url("admin", "services", "emulatorjs"))
end

function action_delete()
    local http = require "luci.http"
    local fs = require "nixio.fs"
    local system = http.formvalue("system")
    local rom = http.formvalue("rom")
    
    if not (system == "nes" or system == "snes" or system == "gba" or system == "arcade") then
        http.status(400, "Invalid system category")
        return
    end
    
    if rom and rom ~= "" then
        -- Sanitize to prevent path traversal
        local filename = rom:gsub("[/\\]", "")
        local filepath = "/www/emulatorjs/roms/" .. system .. "/" .. filename
        if fs.access(filepath) then
            fs.unlink(filepath)
        end
    end
    
    http.redirect(luci.dispatcher.build_url("admin", "services", "emulatorjs"))
end
