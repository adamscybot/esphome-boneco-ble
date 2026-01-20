-- btatt_boneco_device_key.lua
-- Boneco

local p = Proto("boneco", "Boneco")
local pf_device_key = ProtoField.bytes("boneco.device_key", "Device Key")
p.fields = { pf_device_key }

local boneco_exp = ProtoExpert.new(
    "boneco.device_key_found",
    "Boneco device key found",
    expert.group.PROTOCOL,
    expert.severity.NOTE
)
p.experts = { boneco_exp }

local f_btatt_value   = Field.new("btatt.value")
local f_btatt_uuid128 = Field.new("btatt.uuid128")
local f_mac           = Field.new("btle.peripheral_bd_addr")

local DEBUG = false
local WANT_PREFIX  = "06:00:00"
local WANT_UUID128 = "fd:ce:23:47:10:13:41:20:b9:19:1d:bb:32:a2:d1:32"

local win = nil
local last_line = nil

local function ensure_window()
    if not gui_enabled() then return nil end
    if win then return win end

    win = TextWindow.new("Boneco Device Key")
    win:set("Waiting for first match...\n")
    win:set_atclose(function()
        win = nil
        last_line = nil
    end)

    win:add_button("Clear", function()
        if win then win:clear() end
    end)

    -- Prefilter main packet list to only matching frames
    win:add_button("Show key packets", function()
        -- NOTE: safe from button callback; do NOT do this inside dissector()
        set_filter("boneco.device_key")
        apply_filter()
    end)

    return win
end

local function bytes_to_hex_colon(x)
    if x.bytes then return x:bytes():tohex(true, ":") end
    return x:tohex(true, ":")
end

local function has_wanted_uuid128()
    local uuids = { f_btatt_uuid128() }
    if #uuids == 0 then return false end
    for _, u in ipairs(uuids) do
        local ok, hex = pcall(function() return bytes_to_hex_colon(u.value) end)
        if ok and hex == WANT_UUID128 then return true end
        if tostring(u) == WANT_UUID128 then return true end
    end
    return false
end

function p.dissector(tvb, pinfo, tree)
    if not has_wanted_uuid128() then return end

    local vals = { f_btatt_value() }
    if #vals == 0 then return end

    local mac_fi = f_mac()
    local mac = mac_fi and tostring(mac_fi) or ""

    for _, fi in ipairs(vals) do
        local r = fi.range
        if r and r:len() >= 19 and bytes_to_hex_colon(r:range(0, 3)) == WANT_PREFIX then
            local key_range = r:range(3, 16)

            local subtree = tree:add(p, "Boneco")
            subtree:add(pf_device_key, key_range)

            -- Expert info NOTE
            subtree:add_proto_expert_info(boneco_exp, "Boneco device key found")

            -- Tag packet list "Info" column
            local info = tostring(pinfo.cols.info)
            if not info:find("%[Boneco KEY%]") then
                pinfo.cols.info:append(" [Boneco KEY]")
            end

            local key_hex = bytes_to_hex_colon(key_range)
            local line = string.format("[Boneco] MAC=%s  KEY=%s", mac, key_hex)

            if DEBUG then print(line) end

            local w = ensure_window()
            if w and line ~= last_line then
                w:set(line .. "\n")
                last_line = line
            end
        end
    end
end

register_postdissector(p)
