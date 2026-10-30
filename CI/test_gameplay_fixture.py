#!/usr/bin/env python3
"""Exercise isolated Lua fixture controls and persistence without game assets."""
from __future__ import annotations

import ctypes
import ctypes.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

# The real fixture, packetReader and BasePlayer saves run against this native
# API spy. This catches ordering, ownership-gate and stale-persistence failures.
FIXTURE_CHECKS = r"""
package.path = TEST_PROJECT_ROOT .. '/files/mwnet/core-scripts/lib/lua/?.lua;' .. package.path
local savedGetenv = os.getenv
os.getenv = function(key) if key == 'MWNET_GAMEPLAY_TEST' then return '1' end return savedGetenv(key) end
local trace, validators, handlers = {}, {}, {}
local ip = '127.0.0.1'
Players = {[0] = {accountName='MWNetAutoProbe', IsLoggedIn=function() return true end},
           [1] = {accountName='ordinary', IsLoggedIn=function() return true end}}
config = {}
enumerations = {log={INFO=1,WARN=2,ERROR=3}}
customEventHooks = {
 registerValidator=function(name, fn) validators[name]=fn end,
 registerHandler=function(name, fn) handlers[name]=fn end,
 makeEventStatus=function(a,b) return {a,b} end}
mwnet = setmetatable({
 GetDataPath=function() return TEST_DATA_PATH end,
 GetIP=function() return ip end,
 GetCell=function() return 'test' end,
 Resurrect=function(pid, kind) trace[#trace+1]='native-'..pid..'-'..kind; return 'sentinel',17 end,
 SendSpellsActiveChanges=function(pid) trace[#trace+1]='effect-'..pid end,
 AddSpellActive=function(pid,id) assert(id=='mwnet_gameplay_queued_weakness') end,
 LogMessage=function(level, value) assert(not value:find('FIXTURE_ERROR'), value) end,
}, {__index=function() return function() return 0 end end})
dofile(TEST_PROJECT_ROOT .. '/CI/gameplay_fixture.lua')
local function arm() validators.OnPlayerSendMessage({},0,'__mwnet_test die_queued') end
local function clean() trace={} end
local a,b=mwnet.Resurrect(1,2)
assert(a=='sentinel' and b==17 and table.concat(trace,',')=='native-1-2')
clean(); arm(); a,b=mwnet.Resurrect(0,3)
assert(a=='sentinel' and b==17 and table.concat(trace,',')=='effect-0,native-0-3')
clean(); mwnet.Resurrect(0,4); assert(table.concat(trace,',')=='native-0-4')
clean(); arm(); handlers.OnPlayerDisconnect({},0); mwnet.Resurrect(0,5)
assert(table.concat(trace,',')=='native-0-5')
clean(); arm(); ip='192.0.2.1'; mwnet.Resurrect(0,6)
assert(table.concat(trace,',')=='native-0-6')
clean(); ip='127.0.0.1'; arm(); Players[0].accountName='ordinary'; mwnet.Resurrect(0,7)
assert(table.concat(trace,',')=='native-0-7')
print('PASS: queued fixture wrapper ordering, return values, one-shot arm, disconnect cleanup, IP and account gates')
-- Exercise arrangement persistence with the real packetReader and BasePlayer
-- save methods, feeding them values read back from a fake native API.
package.path = TEST_PROJECT_ROOT .. '/files/mwnet/core-scripts/scripts/?.lua;' .. package.path
package.loaded.config = config
package.loaded.patterns = {}
package.loaded.stateHelper = {}
package.loaded.tableHelper = {}
class = function() return {} end
local BasePlayer = require('player.base')
packetReader = require('packetReader')
config.maxAttributeValue, config.maxSpeedValue = 100,100
config.maxSkillValue,config.maxAcrobaticsValue = 100,100
local attrs={'Strength','Intelligence','Willpower','Agility','Speed','Endurance','Personality','Luck'}
local state={attrs={},skills={},stats={}}
local player=Players[0]
player.accountName='MWNetAutoProbe'
player.pid=0
player.data={attributes={},skills={},stats={},shapeshift={}}
setmetatable(player,{__index=BasePlayer})
for i,name in ipairs(attrs) do
 state.attrs[i-1]={base=50,damage=2,modifier=0,skillIncrease=0}
 player.data.attributes[name]={base=50,damage=2,skillIncrease=0}
end
for i=0,26 do
 state.skills[i]={base=5,damage=2,progress=0,modifier=0}
 player.data.skills['Skill'..i]={base=5,damage=2,progress=0}
end
mwnet.GetAttributeCount=function()return 8 end
mwnet.GetAttributeName=function(i)return attrs[i+1] end
mwnet.GetAttributeId=function(name)for i,v in ipairs(attrs)do if v==name then return i-1 end end end
mwnet.GetSkillCount=function()return 27 end
mwnet.GetSkillName=function(i)return 'Skill'..i end
mwnet.GetSkillId=function(name)return tonumber(name:match('Skill(%d+)')) end
for _,v in ipairs({'Base','Damage','Modifier','SkillIncrease'})do
 local field=v:sub(1,1):lower()..v:sub(2)
 mwnet['GetAttribute'..v]=function(pid,i)return state.attrs[i][field] end
 mwnet['SetAttribute'..v]=function(pid,i,value)state.attrs[i][field]=value end
end
mwnet.GetSkillIncrease=function(pid,i)return state.attrs[i].skillIncrease end
for _,v in ipairs({'Base','Damage','Modifier','Progress'})do
 local field=v:sub(1,1):lower()..v:sub(2)
 mwnet['GetSkill'..v]=function(pid,i)return state.skills[i][field] end
 mwnet['SetSkill'..v]=function(pid,i,value)state.skills[i][field]=value end
end
for _,resource in ipairs({'Health','Magicka','Fatigue'})do
 for _,suffix in ipairs({'Base','Current'})do
  local field=resource:lower()..suffix
  mwnet['Get'..resource..suffix]=function()return state.stats[field] end
  mwnet['Set'..resource..suffix]=function(pid,value)state.stats[field]=value end
 end
end
mwnet.IsWerewolf=function()return false end
local writes=0
player.SaveToDrive=function(self)
 writes=writes+1
 self.saved=require('dkjson').decode(require('dkjson').encode(self.data))
end
validators.OnPlayerSendMessage({},0,'__mwnet_test spawn')
assert(writes==1,'Baseline was not persisted once')
for _,name in ipairs(attrs)do assert(player.saved.attributes[name].base==100 and player.saved.attributes[name].damage==0)end
for i=0,26 do assert(player.saved.skills['Skill'..i].base==(i==26 and 100 or 5))end
assert(player.saved.stats.healthBase==200 and player.saved.stats.healthCurrent==200)
assert(player.saved.stats.magickaBase==100 and player.saved.stats.magickaCurrent==100)
assert(player.saved.stats.fatigueBase==400 and player.saved.stats.fatigueCurrent==400)
print('PASS: fixture baseline persists actual readbacks through real packetReader and BasePlayer Save methods')
"""


def lua_string(value: str) -> str:
    separator = ''
    while ']' + separator + ']' in value:
        separator += '='
    return '[' + separator + '[' + value + ']' + separator + ']'


class GameplayFixtureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        name = ctypes.util.find_library('luajit-5.1') or ctypes.util.find_library('luajit')
        if not name:
            raise RuntimeError('LuaJIT runtime is required; install libluajit-5.1-dev')
        cls.lua = ctypes.CDLL(name)
        signatures = {
            'luaL_newstate': ([], ctypes.c_void_p),
            'luaL_openlibs': ([ctypes.c_void_p], None),
            'luaL_loadbuffer': ([ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p], ctypes.c_int),
            'lua_pcall': ([ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int], ctypes.c_int),
            'lua_tolstring': ([ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p], ctypes.c_char_p),
            'lua_close': ([ctypes.c_void_p], None),
        }
        for function, (arguments, result) in signatures.items():
            getattr(cls.lua, function).argtypes = arguments
            getattr(cls.lua, function).restype = result

    def run_lua(self, script: str):
        with tempfile.TemporaryDirectory(prefix='mwnet-fixture-regression-') as temporary:
            root = Path(temporary)
            (root / 'data').mkdir()
            (root / 'scripts').mkdir()
            point = {'cell': 'Synthetic fixture', 'position': [0, 0, 0], 'rotation': [0, 0]}
            metadata = {'schemaVersion': 1, 'spawn': point,
                        'door': {}, 'container': {}, 'combatTarget': {}, 'dialogueTarget': {},
                        'weakness': {'effectId': 31, 'magnitude': 20, 'duration': 120}}
            (root / 'scripts/gameplay-fixture.json').write_text(json.dumps(metadata))
            prefix = ('TEST_PROJECT_ROOT=' + lua_string(str(ROOT)) + '\n'
                      'TEST_DATA_PATH=' + lua_string(str(root / 'data')) + '\n')
            payload = (prefix + script).encode()
            state = self.lua.luaL_newstate()
            self.assertTrue(state, 'LuaJIT could not allocate a state')
            try:
                self.lua.luaL_openlibs(state)
                status = self.lua.luaL_loadbuffer(state, payload, len(payload), b'gameplay-fixture-regression')
                if status == 0:
                    status = self.lua.lua_pcall(state, 0, 0, 0)
                if status:
                    error = self.lua.lua_tolstring(state, -1, None)
                    self.fail(error.decode(errors='replace') if error else 'Lua execution failed')
            finally:
                self.lua.lua_close(state)

    def test_fixture_is_inert_without_opt_in(self):
        self.run_lua("""
os.getenv = function() return nil end
local original = function() return 'unchanged' end
mwnet = {Resurrect = original}
local fixture = dofile(TEST_PROJECT_ROOT .. '/CI/gameplay_fixture.lua')
assert(type(fixture) == 'table' and next(fixture) == nil)
assert(mwnet.Resurrect == original)
""")

    def test_queued_wrapper_and_native_readback_persistence(self):
        self.run_lua(FIXTURE_CHECKS)


if __name__ == '__main__':
    unittest.main()
