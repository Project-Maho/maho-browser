#!/usr/bin/env python3
"""Local per-machine release: build, bundle, manifest, and publish.

Each of the three build machines runs THIS script on its own OS. It detects the
host platform, produces only that platform's release artifacts, generates the
matching update manifest, and idempotently uploads them to the single GitHub
release on the release-only repo. Run it on all three (mac / windows / linux in
WSL) and the one release accumulates every artifact.

  macOS   -> Maho-<v>.dmg + appcast.xml
  Windows -> MahoSetup-<v>.exe + update-win.json
  Linux   -> maho-<v>-x86_64.tar.gz + maho_<v>_amd64.deb + maho-<v>-1.*.rpm (+ .sig)

The release is created on first run and reused afterwards, so machine order does
not matter. Uploads use --clobber so re-runs replace prior assets. Keep the
release draft while every platform uploads; run the final platform invocation
with --finalize only after all expected assets have uploaded.

Prerequisites per machine:
  - A completed toolchain (depot_tools, gclient sync) for the Chromium build.
  - `gh` authenticated locally (gh auth status) with push access to the repo.
  - macOS: signing identity + APPLE_API_KEY/_KEY_ID/_ISSUER (for --notarize),
    SPARKLE_ED_PRIVATE_KEY (for the signed appcast).
  - Windows: MAHO_WIN_CERT_PFX(+_PASSWORD) or MAHO_WIN_CERT_SHA1 for signtool.
  - Linux: dpkg-deb + rpmbuild; gpg key 51C8C6C55603AFF0 for package .sig files
    (public key committed at maho-chromium/build/keys/maho-release-pubkey.asc;
    override id via MAHO_GPG_KEY_ID).
"""

import argparse
import glob
import os
import subprocess
import sys

_SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
_WORKSPACE_ROOT = os.path.normpath(os.path.join(_SCRIPT_DIR, '..', '..', '..'))
_CHROMIUM_SRC = os.path.join(_WORKSPACE_ROOT, 'chromium', 'src')
_VERSION_TXT = os.path.join(_WORKSPACE_ROOT, 'maho', 'version.txt')

_BUILD_MAHO = os.path.join(_SCRIPT_DIR, 'build_maho.py')
_BUILD_LINUX = os.path.join(_SCRIPT_DIR, 'build_maho_linux_packages.py')
_GEN_MANIFESTS = os.path.join(_SCRIPT_DIR, 'generate_update_manifests.py')

DEFAULT_REPO = os.environ.get('MAHO_RELEASE_REPO', 'Project-Maho/release')

_FINAL_RELEASE_ASSETS = (
    'Maho-{version}.dmg',
    'appcast.xml',
    'MahoSetup-{version}.exe',
    'update-win.json',
    'maho-{version}-x86_64.tar.gz',
    'maho-{version}-x86_64.tar.gz.sig',
    'maho_{version}_amd64.deb',
    'maho_{version}_amd64.deb.sig',
    'maho-{version}-1.x86_64.rpm',
    'maho-{version}-1.x86_64.rpm.sig',
)


def host_platform() -> str:
    if sys.platform == 'darwin':
        return 'mac'
    if sys.platform == 'win32':
        return 'win'
    return 'linux'


def read_version() -> str:
    with open(_VERSION_TXT) as f:
        return f.read().strip()


def release_output_dir(out_dir: str) -> str:
    return os.path.join(_CHROMIUM_SRC, out_dir)


def run(cmd, dry_run, **kwargs):
    print('+ ' + ' '.join(cmd))
    if dry_run:
        return 0
    return subprocess.run(cmd, check=True, **kwargs).returncode


def require(path: str) -> str:
    if not os.path.exists(path):
        sys.exit(f'error: expected artifact missing: {path}')
    return path


def build_and_bundle_mac(version, args):
    if not args.skip_build:
        cmd = [
            sys.executable,
            _BUILD_MAHO,
            '--release',
            '--no-launch',
            '--out-dir',
            args.out_dir,
        ]
        if args.notarize:
            cmd.append('--notarize')
        run(cmd, args.dry_run)
    output_dir = release_output_dir(args.out_dir)
    dmg = os.path.join(output_dir, f'Maho-{version}.dmg')
    if not args.dry_run:
        require(dmg)
    run([sys.executable, _GEN_MANIFESTS, '--version', version, '--dmg', dmg,
         '--output-dir', output_dir],
        args.dry_run, cwd=_WORKSPACE_ROOT)
    return [dmg, os.path.join(output_dir, 'appcast.xml')]


def build_and_bundle_win(version, args):
    if not args.skip_build:
        run([
            sys.executable,
            _BUILD_MAHO,
            '--release',
            '--platform',
            'win',
            '--no-launch',
            '--out-dir',
            args.out_dir,
        ], args.dry_run)
    output_dir = release_output_dir(args.out_dir)
    exe = os.path.join(output_dir, f'MahoSetup-{version}.exe')
    if not args.dry_run:
        require(exe)
        require(os.path.join(output_dir, 'maho.exe'))
        require(os.path.join(output_dir, 'maho_mail_helper.exe'))
    run([sys.executable, _GEN_MANIFESTS, '--version', version, '--win-exe', exe,
         '--output-dir', output_dir],
        args.dry_run, cwd=_WORKSPACE_ROOT)
    return [exe, os.path.join(output_dir, 'update-win.json')]


def build_and_bundle_linux(version, args):
    if not args.skip_build:
        run([
            sys.executable,
            _BUILD_LINUX,
            '--release',
            '--format',
            'all',
            '--out-dir',
            args.out_dir,
        ], args.dry_run)
    deb = os.path.join(_CHROMIUM_SRC, 'out', 'Linux-deb', f'maho_{version}_amd64.deb')
    tarball = os.path.join(_CHROMIUM_SRC, 'out', 'Linux-tarball', f'maho-{version}-x86_64.tar.gz')
    rpms = glob.glob(os.path.join(_CHROMIUM_SRC, 'out', 'Linux-rpm', 'rpmbuild', 'RPMS', '*', 'maho-*.rpm'))
    if not args.dry_run:
        require(deb)
        require(tarball)
        if not rpms:
            sys.exit('error: no rpm produced under out/Linux-rpm/rpmbuild/RPMS')
    artifacts = [tarball, deb] + rpms
    if not args.dry_run:
        for artifact in artifacts:
            require(artifact + '.sig')
    return artifacts + [artifact + '.sig' for artifact in artifacts]


def ensure_release(tag, repo, version, dry_run):
    exists = subprocess.run(['gh', 'release', 'view', tag, '--repo', repo],
                            capture_output=True, text=True).returncode == 0
    if exists:
        print(f'release {tag} already exists on {repo}; reusing')
        return
    run(['gh', 'release', 'create', tag, '--repo', repo,
         '--title', f'Maho {version}', '--draft',
         '--notes', f'Maho {version} release.'], dry_run)


def publish(tag, repo, artifacts, dry_run):
    artifacts = [a for a in artifacts if a]
    if not dry_run:
        artifacts = [a for a in artifacts if require(a)]
    run(['gh', 'release', 'upload', tag, '--repo', repo, '--clobber', *artifacts], dry_run)


def finalize_release(tag, repo, dry_run):
    if not dry_run:
        version = tag.removeprefix('v')
        result = subprocess.run(
            [
                'gh',
                'release',
                'view',
                tag,
                '--repo',
                repo,
                '--json',
                'assets',
                '--jq',
                '.assets[].name',
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        uploaded_assets = set(result.stdout.splitlines())
        required_assets = {
            asset.format(version=version) for asset in _FINAL_RELEASE_ASSETS
        }
        missing_assets = sorted(required_assets - uploaded_assets)
        if missing_assets:
            sys.exit(
                'error: cannot finalize release; missing required release assets: '
                + ', '.join(missing_assets)
            )
    run(['gh', 'release', 'edit', tag, '--repo', repo, '--draft=false', '--latest'], dry_run)


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--repo', default=DEFAULT_REPO, help='release-only repo (owner/name).')
    p.add_argument(
        '--out-dir',
        default='out/Default',
        help='Chromium output directory relative to chromium/src.',
    )
    p.add_argument('--notarize', action='store_true',
                   help='macOS: notarize + staple the dmg (needs APPLE_API_* env).')
    p.add_argument('--skip-build', action='store_true',
                   help='Reuse an already-built bundle; only manifest + publish.')
    p.add_argument('--no-publish', action='store_true',
                   help='Build + bundle + manifest, but do not touch GitHub.')
    p.add_argument(
        '--finalize',
        action='store_true',
        help='Make the draft release public/latest after all platform assets upload.',
    )
    p.add_argument('--dry-run', action='store_true',
                   help='Print every command without executing it.')
    args = p.parse_args()

    platform = host_platform()
    version = read_version()
    tag = f'v{version}'
    print(f'== Maho local release: platform={platform} version={version} '
          f'tag={tag} repo={args.repo} ==')

    builder = {
        'mac': build_and_bundle_mac,
        'win': build_and_bundle_win,
        'linux': build_and_bundle_linux,
    }[platform]
    artifacts = builder(version, args)

    print('Artifacts:')
    for a in artifacts:
        print(f'  {a}')

    if args.no_publish:
        print('--no-publish set; skipping GitHub release.')
        return 0

    ensure_release(tag, args.repo, version, args.dry_run)
    publish(tag, args.repo, artifacts, args.dry_run)
    if args.finalize:
        finalize_release(tag, args.repo, args.dry_run)
    print(f'Published {platform} artifacts to {args.repo} {tag}.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
