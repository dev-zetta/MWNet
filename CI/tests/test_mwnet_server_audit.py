import contextlib
import io
import json
from pathlib import Path
import sys
import unittest
from unittest import mock

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'docker/discovery/host-diagnostics'))
import verify_server as audit


class AuditPrivacyTests(unittest.TestCase):
    def test_private_docker_values_are_excluded_from_selected_output(self):
        secret='PRIVATE-CANARY-DO-NOT-EXPORT'
        raw={'Id':'id','Image':'image','RestartCount':0,'Mounts':[],
             'State':{'Running':True,'Status':'running','OOMKilled':False,'ExitCode':0,'StartedAt':'time','Error':secret},
             'Config':{'User':'10001','Env':['PASSWORD='+secret],'Cmd':[secret],'Labels':{'secret':secret}},
             'HostConfig':{'ReadonlyRootfs':True,'Privileged':False,'Memory':512*1024**2,'MemorySwap':1024**3,'NanoCpus':10**9,
                           'PidsLimit':128,'RestartPolicy':{'Name':'unless-stopped'},'NetworkMode':'bridge',
                           'LogConfig':{'Type':'json-file','Config':{'max-size':'10m','max-file':'3','private':secret}}}}
        result=audit.selected_container(raw)
        self.assertNotIn(secret,json.dumps(result))
        self.assertNotIn('Env',json.dumps(result))

    def test_malformed_private_config_cannot_export_parser_text(self):
        secret='PRIVATE-CANARY-DO-NOT-EXPORT'
        stream=io.StringIO()
        with mock.patch('pathlib.Path.read_text',return_value=secret), contextlib.redirect_stdout(stream):
            exec(audit.GAME_CHECK,{'__name__':'__audit_test__'})
        self.assertEqual(json.loads(stream.getvalue()),{'inspectionComplete':False})
        self.assertNotIn(secret,stream.getvalue())

    def test_running_with_oom_or_without_resource_limits_fails(self):
        selected={'running':True,'oomKilled':True,'restartCount':0,'restartPolicy':'unless-stopped',
                  'memoryBytes':0,'nanoCpus':0,'pidsLimit':0,'logConfig':{},'privileged':False,'networkMode':'host'}
        checks=audit.container_checks('playit',selected)
        self.assertFalse(checks['runningWithoutOom'])
        self.assertFalse(checks['resourceLimits'])
        self.assertFalse(checks['boundedLogs'])


if __name__=='__main__':
    unittest.main()
