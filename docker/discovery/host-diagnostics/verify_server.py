#!/usr/bin/env python3
"""NUC-only read-only server audit; optional separately backed-up no-sleep policy."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import pwd
import stat
import subprocess
import sys

from configure import command, private_directory, storage_health

RUNTIME = 'a6868387dc80dcafd9b8ebad3d8c708511d9a615'
MANIFEST_SHA = '3cd7644a5a42eb5f43acb95451699b7fe4b21629e21530ed3f61a32d67df8843'
RELEASE = Path('/srv/mwnet/releases') / RUNTIME
DEPLOYMENT = Path('/home/gmax/mwnet-stable-update-20261105/bundle/deployment.json')
NAMES = {'game': 'mwnet-registration-game-1', 'directory': 'mwnet-directory-directory-1',
         'https': 'mwnet-directory-https-1', 'playit': 'mwnet-playit-playit-1'}

# This runs inside the existing game container. No private value or parser error
# leaves the container: only a fixed boolean/numeric schema is printed.
GAME_CHECK = r'''
import configparser, hashlib, json, stat
from pathlib import Path
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
result = {'inspectionComplete': False}
try:
    c = configparser.ConfigParser(interpolation=None)
    c.read_string(Path('/state/xdg-config/openmw/mwnet-server.cfg').read_text())
    expected = {'General': {'localAddress':'0.0.0.0','publicListen':'true','port':'25565','maximumPlayers':'16','hostname':'MWNet 1.0 server'},
                'Plugins': {'home':'/state/server','plugins':'serverCore.lua'},
                'Discovery': {'directoryUrl':'https://mwnet.playit.plus','announce':'true','localTest':'false','publicAddress':'209.25.141.17','publicPort':'43133'}}
    checks = {section+'.'+key: c.get(section,key,fallback=None)==value for section, values in expected.items() for key,value in values.items()}
    h = Path('/state/access-password.hash').read_text().strip()
    checks['passwordHashMatchesStoredArgon2id'] = h.startswith('$argon2id$') and c.get('General','passwordHash',fallback='')==h
    checks['noPlaintextPasswordInConfig'] = c.get('General','password',fallback='')==''
    for name in ('access-password.txt','access-password.hash','xdg-config/openmw/mwnet-server.cfg'):
        p=Path('/state')/name
        s=p.lstat()
        checks['privateMode.'+name] = stat.S_ISREG(s.st_mode) and stat.S_IMODE(s.st_mode)==0o600 and s.st_uid==10001
    checks['persistentXdgRedirect'] = 'config="/state/xdg-config/openmw"' in Path('/opt/mwnet/build/openmw.cfg').read_text()
    originals = {str(p.relative_to('/opt/mwnet/files/mwnet/core-scripts')): sha(p) for group in ('scripts','lib') for p in (Path('/opt/mwnet/files/mwnet/core-scripts')/group).rglob('*') if p.is_file()}
    checks['managedCodeMatches'] = len(originals)==48 and all(sha(Path('/state/server')/name)==value for name,value in originals.items())
    running=[]
    for p in Path('/proc').glob('[0-9]*/exe'):
        try:
            if str(p.resolve()).endswith('/mwnet-server'): running.append(sha(p))
        except (OSError, RuntimeError): pass
    result = {'inspectionComplete':True, 'checks':checks, 'managedCodeFiles':len(originals),
              'serverSha256':sha(Path('/opt/mwnet/build/mwnet-server')), 'liveServerExecutableHashes':running}
except Exception:
    pass
print(json.dumps(result))
'''


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def docker(*args, timeout=30):
    return command(['docker', '--context', 'default', *args], timeout=timeout).stdout


def selected_container(raw):
    """Whitelist selected Docker fields; never return Env/Cmd/labels/errors."""
    host, state, config = raw['HostConfig'], raw['State'], raw['Config']
    return {'id':raw['Id'], 'imageId':raw['Image'], 'running':state['Running'],
            'status':state['Status'], 'oomKilled':state['OOMKilled'], 'exitCode':state['ExitCode'],
            'startedAt':state['StartedAt'], 'restartCount':raw['RestartCount'],
            'health':state.get('Health', {}).get('Status'), 'user':config['User'],
            'readonlyRootfs':host['ReadonlyRootfs'], 'privileged':host['Privileged'],
            'capDrop':host.get('CapDrop'), 'capAdd':host.get('CapAdd'), 'securityOpt':host.get('SecurityOpt'),
            'memoryBytes':host['Memory'], 'memorySwapBytes':host['MemorySwap'], 'nanoCpus':host['NanoCpus'],
            'pidsLimit':host['PidsLimit'], 'restartPolicy':host['RestartPolicy']['Name'],
            'networkMode':host['NetworkMode'], 'portBindings':host.get('PortBindings'),
            'logConfig':{'Type':host['LogConfig']['Type'], 'Config':{k:v for k,v in host['LogConfig'].get('Config',{}).items() if k in ('max-size','max-file')}},
            'mounts':[{'type':m['Type'], 'target':m['Destination'], 'writable':m['RW']} for m in raw['Mounts']]}


def container_checks(role, selected):
    limits = {'game':(512,128),'directory':(512,64),'https':(128,64),'playit':(256,128)}
    memory, pids = limits[role]
    checks = {'runningWithoutOom':selected['running'] and not selected['oomKilled'],
              'noRestarts':selected['restartCount']==0, 'restartUnlessStopped':selected['restartPolicy']=='unless-stopped',
              'resourceLimits':selected['memoryBytes']==memory*1024**2 and selected['nanoCpus']==10**9 and selected['pidsLimit']==pids,
              'boundedLogs':selected['logConfig']=={'Type':'json-file','Config':{'max-size':'10m','max-file':'3'}},
              'notPrivileged':not selected['privileged']}
    if role != 'playit':
        checks.update(readonlyRoot=selected['readonlyRootfs'], droppedAllCapabilities='ALL' in (selected['capDrop'] or []),
                      noNewPrivileges=any(v in ('no-new-privileges','no-new-privileges=true','no-new-privileges:true') for v in selected['securityOpt'] or []))
    if role in ('game','directory'):
        checks['nonRoot'] = selected['user'] in ('10001','10001:10001')
    if role == 'game':
        checks['loopbackUdpOnly'] = selected['portBindings']=={'25565/udp':[{'HostIp':'127.0.0.1','HostPort':'25565'}]}
    elif role == 'directory':
        checks['noHostPublishedPorts'] = not selected['portBindings']
    elif role == 'https':
        checks['loopbackHttpsIngress'] = selected['portBindings']=={str(p)+'/tcp':[{'HostIp':'127.0.0.1','HostPort':str(p)}] for p in (80,443)}
    else:
        checks['hostNetworkAsReviewed'] = selected['networkMode']=='host'
    return checks


def audit():
    if sha(DEPLOYMENT) != MANIFEST_SHA:
        raise ValueError('Accepted deployment manifest identity differs')
    manifest=json.loads(DEPLOYMENT.read_text())
    if manifest['runtimeSourceCommit'] != RUNTIME or manifest['version'] != '1.0.0':
        raise ValueError('Accepted runtime selection differs')
    results={'runtimeSourceCommit':RUNTIME, 'deploymentManifestSha256':MANIFEST_SHA, 'containers':{}, 'checks':{}}
    # The agent project name may differ; identify exactly one MWNet-owned agent.
    agents=docker('ps','--filter','label=com.docker.compose.service=playit','--format','{{.Names}}').splitlines()
    agents=[name for name in agents if name.startswith('mwnet-') and name.endswith('-playit-1')]
    if len(agents)!=1:
        raise ValueError('Expected exactly one running MWNet ingress agent')
    names=dict(NAMES,playit=agents[0])
    raw_containers={}
    for role,name in names.items():
        raw=json.loads(docker('inspect',name))[0]
        selected=selected_container(raw)
        results['containers'][role]={'state':selected, 'checks':container_checks(role,selected)}
        raw_containers[role]=raw
    game=raw_containers['game']
    results['checks']['gameBaseImageAccepted']=game['Image'] in manifest['acceptedGameImageIds']
    # Compare only accepted release-code inputs, never world/accounts/assets/configs.
    code={name:value for name,value in manifest['inputs'].items() if name.startswith('server-package/') or name in ('entrypoint.py','verify_listing.py','mwnet-directory')}
    results['checks']['installedReleaseCodeMatches']=all(not (RELEASE/name).is_symlink() and sha(RELEASE/name)==value for name,value in code.items())
    results['releaseCodeInputsChecked']=len(code)
    mounts={m['Destination']:m for m in game['Mounts']}
    expected={'/opt/mwnet':'server-package','/opt/registration/entrypoint.py':'entrypoint.py','/opt/registration/verify_listing.py':'verify_listing.py'}
    results['checks']['acceptedReadonlyCodeMounts']=all(mounts[d]['Source']==str(RELEASE/s) and not mounts[d]['RW'] for d,s in expected.items())
    results['checks']['persistentState']=mounts['/state']['Type']=='volume' and mounts['/state']['RW']
    results['checks']['ownedContentReadonly']=not mounts['/game-content']['RW']
    scratch=mounts['/tmp']
    results['checks']['gameScratchOnDisk']=scratch['Type']=='bind' and scratch['RW'] and command(['stat','-f','-c','%T',scratch['Source']]).stdout.strip() not in ('tmpfs','ramfs')
    dm={m['Destination']:m for m in raw_containers['directory']['Mounts']}
    results['checks']['directoryStatePersistent']=dm['/data']['Type']=='volume' and dm['/data']['RW']
    results['checks']['acceptedReadonlyDirectoryMount']=dm['/usr/local/bin/mwnet-directory']['Source']==str(RELEASE/'mwnet-directory') and not dm['/usr/local/bin/mwnet-directory']['RW']
    args=raw_containers['directory']['Config']['Cmd']
    results['checks']['directoryCommandAsReviewed']=args==['--origin','https://mwnet.playit.plus','--bind','0.0.0.0','--database','/data/directory.sqlite3','--trust-proxy']
    hm={m['Destination']:m for m in raw_containers['https']['Mounts']}
    results['checks']['httpsStatePersistent']=all(hm[d]['Type']=='volume' and hm[d]['RW'] for d in ('/data','/config'))
    caddy=hm['/etc/caddy/Caddyfile']
    results['checks']['acceptedReadonlyHttpsConfiguration']=not caddy['RW'] and sha(Path(caddy['Source']))=='486028cbef7e0de8404c055c75d1408b843c5cee66bad69c2388cc1a90c0b03c'
    probe=json.loads(command(['docker','--context','default','exec','-i',NAMES['game'],'python3','-B','-c',GAME_CHECK]).stdout)
    results['gameConfiguration']=probe
    results['checks']['gameConfigurationVerified']=probe.get('inspectionComplete') is True and all(probe['checks'].values())
    results['checks']['runningServerIdentityMatches']=probe.get('serverSha256')==manifest['serverSha256'] and probe.get('liveServerExecutableHashes')==[manifest['serverSha256']]
    directory_hash=docker('exec',NAMES['directory'],'sha256sum','/usr/local/bin/mwnet-directory').split()[0]
    results['directorySha256']=directory_hash
    results['checks']['directoryIdentityMatches']=directory_hash==manifest['directorySha256']
    health=command(['docker','--context','default','exec',NAMES['directory'],'curl','--fail','--silent','--max-time','5','http://127.0.0.1:8080/healthz'], check=False)
    results['checks']['directoryHealthEndpoint']=health.returncode==0
    results['privateFilePermissions']={}
    for name in ('/etc/playit/agent.env','/etc/mwnet-directory.env','/etc/mwnet-gameplay/content.env','/home/gmax/mwnet-registration/.env'):
        try:
            s=Path(name).lstat()
            results['privateFilePermissions'][name]={'regular':stat.S_ISREG(s.st_mode),'mode':oct(stat.S_IMODE(s.st_mode)), 'uid':s.st_uid,
                                                   'protected':stat.S_ISREG(s.st_mode) and not s.st_mode & 0o077 and s.st_uid in (0,1000)}
        except FileNotFoundError:
            results['privateFilePermissions'][name]={'protected':False,'missing':True}
    results['checks']['secretFilesProtected']=all(x['protected'] for x in results['privateFilePermissions'].values())
    backup=Path('/var/backups/mwnet-release/20261105T134917Z')
    results['checks']['originalDeploymentBackupsRetained']=all((backup/name).is_file() for name in ('game-state.tar.gz','directory-state.tar.gz','game-compose.yml','base-compose.yml','directory-compose.yml'))
    results['nvmeHealth']=storage_health()
    results['checks']['nvmeNoCriticalOrMediaErrors']=not results['nvmeHealth']['healthWarningsPresent']
    results['baselineConfigurationPassed']=all(results['checks'].values()) and all(all(r['checks'].values()) for r in results['containers'].values())
    results['securityScope']='Playit retains reviewed host-network/writable-root/default-capability posture; no comprehensive security or gameplay acceptance.'
    return results


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--disable-sleep',action='store_true')
    args=parser.parse_args()
    os.umask(0o077)
    if os.geteuid()!=0 or Path('/sys/class/dmi/id/product_name').read_text().strip()!='NUC8i5BEH':
        raise ValueError('Run with interactive sudo on the reviewed NUC8i5BEH')
    if Path('/sys/class/dmi/id/bios_version').read_text().strip()!='BECFL357.86A.0098.2026.0204.1428':
        raise ValueError('Post-update BIOS identity differs')
    here=Path(__file__).resolve().parent
    if here!=Path('/home/gmax/mwnet-post-bios-20261109') or command(['stat','-f','-c','%T',str(here)]).stdout.strip() in ('tmpfs','ramfs'):
        raise ValueError('Use the checked operator bundle on disk')
    pins=json.loads((here/'manifest.json').read_text())['files']
    if set(pins)!= {'verify_server.py','no_sleep.py','configure.py'} or any((here/n).is_symlink() or sha(here/n)!=h for n,h in pins.items()):
        raise ValueError('Verification bundle changed')
    report=here/'verification-result.json'
    descriptor=os.open(report,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o640)
    operator=pwd.getpwnam('gmax')
    os.fchown(descriptor,operator.pw_uid,operator.pw_gid)
    result={'observedUtc':datetime.now(timezone.utc).isoformat(),'bootId':Path('/proc/sys/kernel/random/boot_id').read_text().strip(),'toolHashes':pins}
    try:
        if args.disable_sleep:
            result['sleepPolicy']=json.loads(command([sys.executable,'-B',str(here/'no_sleep.py'),'apply']).stdout)
        result['serverAudit']=audit()
        result['status']='verified' if result['serverAudit']['baselineConfigurationPassed'] else 'findings'
    except Exception as error:
        # Do not serialize command output, config parser text or raw exceptions.
        result.update(status='inspection-incomplete',errorType=type(error).__name__)
    with os.fdopen(descriptor,'w') as stream:
        json.dump(result,stream,indent=2); stream.write('\n'); stream.flush(); os.fsync(stream.fileno())
    print('Selected safe result saved: '+str(report))
    return 0 if result['status']=='verified' else 1


if __name__=='__main__':
    sys.exit(main())
