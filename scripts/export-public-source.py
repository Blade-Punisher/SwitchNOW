#!/usr/bin/env python3
"""Export reviewed public inputs, never the working directory or Git history."""
import hashlib
import json
import os
import shutil
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'dist' / 'SwitchNOW-1.3.0-source'
ARCHIVE = ROOT / 'dist' / 'SwitchNOW-1.3.0-source.zip'
PUBLIC_DOCS = {
    'BUILD.md', 'PUBLISHING.md', 'SECURITY.md', 'RELEASE_NOTES_1.3.0_EN.md',
    'RELEASE_NOTES_1.3.0_RU.md', 'AUTOLOGIN_R70_RU.md',
    'KEYBOARD_SHORTCUTS_R70_RU.md', 'EPIC_AUTOLOGIN_R71_RU.md',
}
ROOT_FILES = {'.gitignore', 'CMakeLists.txt', 'build-switch.ps1',
              'README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md'}
PUBLIC_SCRIPTS = {'build-switch-msys2.sh', 'package-release.ps1',
                  'package-stable-release.ps1', 'export-public-source.py',
                  'audit-public-release.py', 'localization_catalog.py',
                  'ui_string_inventory.py', 'run-host-tests.sh'}
BLOCK_DIRS = {'.git', 'node_modules', '__pycache__', '.cache', '.idea', '.vscode',
              'CMakeFiles', 'autom4te.cache'}
BLOCK_SUFFIXES = {'.log', '.tmp', '.bak', '.pyc', '.obj', '.o', '.exe', '.elf',
                  '.pdb', '.nacp', '.pem', '.key', '.keys', '.enc', '.p12', '.pfx'}

def selected(path):
    p = path.relative_to(ROOT)
    parts = p.parts
    if any(x in BLOCK_DIRS for x in parts) or path.is_symlink():
        return False
    if path.suffix.lower() in BLOCK_SUFFIXES or path.name.startswith('.env'):
        return False
    if len(parts) == 1:
        return p.name in ROOT_FILES
    top = parts[0]
    if top in {'app', 'localization', 'resources', 'tests'}:
        return True
    if top == '.github':
        return parts[1] == 'workflows'
    if top == 'scripts':
        return p.name in PUBLIC_SCRIPTS
    if top == 'docs':
        return (p.name in PUBLIC_DOCS or parts[1] in {'branding', 'autologin', 'licenses'})
    if top == 'extern':
        # Public Mbed TLS private-key fixtures are unnecessary for this build.
        if parts[1:5] == ('libpeer', 'third_party', 'mbedtls', 'tests'):
            return False
        if path.suffix in {'.nro', '.zip', '.tar', '.gz', '.7z'}:
            return False
        return True
    if top == 'tools' and parts[1] == 'switchnow-forwarder-installer':
        if path.suffix == '.nro':
            return p.name == 'switchnow-forwarder-installer.nro'
        return True
    return False

def main():
    expected = ROOT / 'dist' / 'SwitchNOW-1.3.0-source'
    assert OUT.resolve() == expected.resolve() and OUT.parent == ROOT / 'dist'
    files = []
    allowed_roots = {'.github', 'app', 'localization', 'resources', 'tests',
                     'scripts', 'docs', 'extern', 'tools'}
    for directory, dirs, names in os.walk(ROOT):
        base = Path(directory)
        dirs[:] = [d for d in dirs if d not in BLOCK_DIRS and
                   (base != ROOT or d in allowed_roots)]
        for name in names:
            p = base / name
            if selected(p):
                files.append(p)
    OUT.mkdir(parents=True, exist_ok=True)
    old_manifest_path = OUT / 'SOURCE_MANIFEST.json'
    old_manifest = json.loads(old_manifest_path.read_text()) if old_manifest_path.exists() else {}
    included = {p.relative_to(ROOT).as_posix() for p in files}
    for old in OUT.rglob('*'):
        if old.is_file() and old.name != 'SOURCE_MANIFEST.json' and old.relative_to(OUT).as_posix() not in included:
            old.unlink()
    manifest = {}
    for source in files:
        target = OUT / source.relative_to(ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        key = source.relative_to(ROOT).as_posix()
        digest = hashlib.sha256(source.read_bytes()).hexdigest()
        manifest[key] = digest
        if not target.is_file() or old_manifest.get(key) != digest:
            shutil.copyfile(source, target)
    manifest = dict(sorted(manifest.items()))
    (OUT / 'SOURCE_MANIFEST.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    with zipfile.ZipFile(ARCHIVE, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for name in [*manifest, 'SOURCE_MANIFEST.json']:
            z.write(OUT / name, name)
    print(f'Exported {len(manifest)} public inputs: {ARCHIVE.name}')

if __name__ == '__main__':
    main()
