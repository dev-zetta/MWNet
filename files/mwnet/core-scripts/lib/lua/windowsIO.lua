-- Read-only UTF-8 file access for the JSON interface. Persistence writes use
-- MWNet's native atomic writer. No separately distributed Lua DLL is needed.
local ffi = require("ffi")
if ffi.os ~= "Windows" then return io end

ffi.cdef[[
int __stdcall MultiByteToWideChar(unsigned int, unsigned long, const char *, int, wchar_t *, int);
void * __stdcall CreateFileW(const wchar_t *, unsigned long, unsigned long, void *, unsigned long, unsigned long, void *);
int __stdcall ReadFile(void *, void *, unsigned long, unsigned long *, void *);
int __stdcall CloseHandle(void *);
unsigned long __stdcall GetLastError(void);
]]

local kernel = ffi.load("kernel32")
local invalidHandle = ffi.cast("void *", -1)
local methods = {}
local windowsIO = {}

local function failure(operation)
    return nil, operation .. " failed (Windows error " .. tonumber(kernel.GetLastError()) .. ")"
end

function windowsIO.open(filename, mode)
    if mode ~= nil and mode ~= "r" and mode ~= "rb" then
        return nil, "windowsIO only supports reading; use MWNet's atomic writer"
    end
    if type(filename) ~= "string" or filename:find("\0", 1, true) then
        return nil, "invalid filename"
    end
    -- CP_UTF8, MB_ERR_INVALID_CHARS; include the terminating NUL in both calls.
    local length = kernel.MultiByteToWideChar(65001, 8, filename, -1, nil, 0)
    if length == 0 then return failure("UTF-8 filename conversion") end
    local wide = ffi.new("wchar_t[?]", length)
    if kernel.MultiByteToWideChar(65001, 8, filename, -1, wide, length) == 0 then
        return failure("UTF-8 filename conversion")
    end
    -- GENERIC_READ, FILE_SHARE_READ | WRITE | DELETE, OPEN_EXISTING.
    local handle = kernel.CreateFileW(wide, 0x80000000, 7, nil, 3, 0x80, nil)
    if handle == invalidHandle then return failure("Opening file") end
    return setmetatable({ handle = ffi.gc(handle, kernel.CloseHandle) }, { __index = methods })
end

function methods:read(format)
    assert(self.handle ~= nil, "attempt to read a closed file")
    assert(format == "*all" or format == "*a", "windowsIO only supports reading the complete file")
    local buffer = ffi.new("char[65536]")
    local count = ffi.new("unsigned long[1]")
    local chunks = {}
    while true do
        if kernel.ReadFile(self.handle, buffer, 65536, count, nil) == 0 then
            return failure("Reading file")
        end
        local size = tonumber(count[0])
        if size == 0 then break end
        chunks[#chunks + 1] = ffi.string(buffer, size)
    end
    return table.concat(chunks)
end

function methods:close()
    assert(self.handle ~= nil, "attempt to close a closed file")
    local handle = ffi.gc(self.handle, nil)
    self.handle = nil
    if kernel.CloseHandle(handle) == 0 then return failure("Closing file") end
    return true
end

return windowsIO
