#!/usr/bin/env python3
"""Offline export checks. Findings contain paths/rules, never secret values."""
import hashlib
import json
import os
import re
import struct
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RULES = {
    'personal-path': re.compile(rb'[A-Za-z]:[/\\]+Users[/\\]+', re.I),
    'github-token': re.compile(rb'(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,})'),
    'service-secret': re.compile(rb'(?:AKIA[0-9A-Z]{16}|sk-(?:proj-)?[A-Za-z0-9_-]{32,})'),
    'live-jwt': re.compile(rb'eyJ[A-Za-z0-9_-]{10,}\.eyJ[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{20,}'),
}
PEM = re.compile(rb'-----BEGIN ((?:RSA |EC |OPENSSH |ENCRYPTED )?PRIVATE KEY)-----[\r\n]+(?:[A-Za-z0-9+/=]{16,}[\r\n]+){2,}-----END \1-----')
# Exact public homebrew self-signing input, verified against upstream rsa_keys.h.
# This is not a console, account or developer authentication key.
PUBLIC_HOMEBREW_KEY = 'c62a7599df0fe31876e38c10c27b82e22fd62411234cf8dd4e7b259887cffd95'
PUBLIC_KEY_PATHS = {'switch/SwitchNOW/SwitchNOW.nro',
                    'tools/switchnow-forwarder-installer/romfs/hacbrewpack.wasm',
                    'tools/switchnow-forwarder-installer/switchnow-forwarder-installer.nro'}
PRIVATE_NAMES = {'auth_accounts.json', 'auth_session.json', 'native_credentials.json',
                 'stream_settings.json', 'prod.keys', 'title.keys', 'connection_debug.log',
                 'runtime.log', 'first_frame.ppm'}

def scan(name, data, findings):
    path = Path(name)
    if any(p in {'.git', 'node_modules', '__pycache__'} for p in path.parts):
        findings.append((name, 'private/build-directory'))
    if path.name in PRIVATE_NAMES or path.suffix in {'.log', '.keys', '.pem', '.key', '.pfx', '.p12'}:
        findings.append((name, 'runtime-or-key-file'))
    for term in os.environ.get('SWITCHNOW_AUDIT_PRIVATE_TERMS', '').splitlines():
        if term and term.lower().encode() in data.lower():
            findings.append((name, 'private-term'))
    for label, pattern in RULES.items():
        if pattern.search(data):
            findings.append((name, label))
    for match in PEM.finditer(data):
        if name not in PUBLIC_KEY_PATHS or hashlib.sha256(match.group()).hexdigest() != PUBLIC_HOMEBREW_KEY:
            findings.append((name, 'unreviewed-private-key-data'))

def main():
    findings = []
    checks = {}
    for suffix in ('', '-source'):
        archive = ROOT / f'dist/SwitchNOW-1.3.0{suffix}.zip'
        with zipfile.ZipFile(archive) as z:
            assert z.testzip() is None
            assert len(z.namelist()) == len(set(z.namelist()))
            for name in z.namelist():
                assert not name.startswith('/') and '..' not in Path(name).parts
                scan(name, z.read(name), findings)
            checks[archive.name] = len(z.namelist())
            if suffix:
                manifest = json.loads(z.read('SOURCE_MANIFEST.json'))
                for name, digest in manifest.items():
                    assert hashlib.sha256(z.read(name)).hexdigest() == digest, name
            else:
                assert z.read('switch/SwitchNOW/SwitchNOW.nro') == (ROOT/'dist/SwitchNOW-1.3.0.nro').read_bytes()
    nro = (ROOT/'dist/SwitchNOW-1.3.0.nro').read_bytes()
    assert nro[0x10:0x14] == b'NRO0'
    asset = struct.unpack_from('<I', nro, 0x18)[0]
    assert nro[asset:asset+4] == b'ASET'
    nacp_offset, nacp_size = struct.unpack_from('<QQ', nro, asset+24)
    nacp = nro[asset+nacp_offset:asset+nacp_offset+nacp_size]
    assert nacp[0x200:0x300].split(b'\0')[0] == b'Blade-Punisher'
    assert nacp[0x3060:0x3070].split(b'\0')[0] == b'1.3.0'
    report = {'archives': checks, 'findings': findings, 'nacp_author': 'Blade-Punisher',
              'version': '1.3.0', 'hardware_tested': False,
              'reviewed_public_component': {'purpose': 'hacBrewPack homebrew self-signing',
                  'pem_sha256': PUBLIC_HOMEBREW_KEY,
                  'upstream': 'https://github.com/rlaphoenix/hacBrewPack/blob/master/rsa_keys.h',
                  'details': 'docs/SECURITY.md'},
              'scope': 'Known personal identifiers, runtime/key files, common token/private-key signatures, archive CRC, source hashes and NACP. Public protocol IDs, trust anchors, copyright contacts and synthetic test inputs are retained.'}
    (ROOT/'build/130-public-audit.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))
    assert not findings, 'Review findings before publication'

if __name__ == '__main__':
    main()
