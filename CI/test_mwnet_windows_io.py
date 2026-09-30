#!/usr/bin/env python3
"""Exercise the bundled UTF-8 JSON reader with the Windows SDK's LuaJIT DLL."""

import argparse
import ctypes
import json
import os
from pathlib import Path
import tempfile


def lua_string(value):
    # Decimal byte escapes work with Lua 5.1, including non-ASCII UTF-8 paths.
    return '"' + ''.join(f'\\{byte:03d}' for byte in str(value).encode('utf-8')) + '"'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dll', type=Path)
    parser.add_argument('--source-dir', type=Path, default=Path('.'))
    args = parser.parse_args()
    dll = args.dll.resolve()
    modules = (args.source_dir / 'files/mwnet/core-scripts/lib/lua').resolve()
    with os.add_dll_directory(str(dll.parent)):
        lua = ctypes.CDLL(str(dll))
    state_type = ctypes.c_void_p
    lua.luaL_newstate.restype = state_type
    lua.luaL_openlibs.argtypes = [state_type]
    lua.luaL_loadstring.argtypes = [state_type, ctypes.c_char_p]
    lua.lua_pcall.argtypes = [state_type, ctypes.c_int, ctypes.c_int, ctypes.c_int]
    lua.lua_tolstring.argtypes = [state_type, ctypes.c_int, ctypes.POINTER(ctypes.c_size_t)]
    lua.lua_tolstring.restype = ctypes.c_char_p
    lua.lua_close.argtypes = [state_type]
    with tempfile.TemporaryDirectory(prefix='mwnet-windows-io-') as tmp:
        data = Path(tmp) / 'Žluťoučký_玩家_🧪'
        data.mkdir()
        value = {'message': 'Příliš žluťoučký 玩家 🧪', 'large': 'x' * 150000}
        (data / '玩家.json').write_text(json.dumps(value, ensure_ascii=False), encoding='utf-8')
        (data / 'empty.json').write_bytes(b'')
        script = f'''
package.path = {lua_string(modules / '?.lua')} .. ';' .. package.path
local reader = require('windowsIO')
local filename = {lua_string(data / '玩家.json')}
local file = assert(reader.open(filename, 'r'))
local text = assert(file:read('*all'))
assert(#text > 150000)
assert(file:close())
assert(not pcall(function() file:read('*all') end))
assert(not pcall(function() file:close() end))
local empty = assert(reader.open({lua_string(data / 'empty.json')}, 'rb'))
assert(empty:read('*a') == '')
assert(empty:close())
assert(reader.open({lua_string(data / 'missing.json')}) == nil)
assert(reader.open(filename, 'w') == nil)
assert(reader.open(filename .. string.char(0)) == nil)
assert(reader.open(filename .. string.char(255)) == nil)
-- Exercise the real JSON adapter, not only the low-level reader.
require('utils')
enumerations = {{ log = {{ ERROR = 1 }} }}
mwnet = {{ LogMessage = function() end }}
config = {{ dataPath = {lua_string(data)} }}
local json = require('jsonInterface')
json.setLibrary(reader)
local decoded = assert(json.load({lua_string('玩家.json')}))
assert(decoded.message == {lua_string(value['message'])})
assert(decoded.large == string.rep('x', 150000))
assert(json.load('missing.json') == nil)
collectgarbage('collect')
'''
        state = lua.luaL_newstate()
        if not state:
            raise RuntimeError('Could not create LuaJIT state')
        try:
            lua.luaL_openlibs(state)
            result = lua.luaL_loadstring(state, script.encode('utf-8'))
            if result == 0:
                result = lua.lua_pcall(state, 0, 0, 0)
            if result != 0:
                raise RuntimeError(lua.lua_tolstring(state, -1, None).decode('utf-8', errors='replace'))
        finally:
            lua.lua_close(state)
    print('Windows UTF-8 paths, JSON decoding, multi-buffer reads, empty/missing files, invalid paths and closed handles passed')


if __name__ == '__main__':
    main()
