#!/usr/bin/env python3
"""Build and package Maho Browser for Linux distributions."""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

_SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
_WORKSPACE_ROOT = os.path.normpath(os.path.join(_SCRIPT_DIR, '..', '..', '..'))
_CHROMIUM_SRC = os.path.join(_WORKSPACE_ROOT, 'chromium', 'src')
_MAHO_CHROMIUM = os.path.join(_WORKSPACE_ROOT, 'maho-chromium')
_BUILD_MAHO_PY = os.path.join(_SCRIPT_DIR, 'build_maho.py')
_RELEASE_PUBLIC_KEY = os.path.join(
    _MAHO_CHROMIUM,
    'build',
    'keys',
    'maho-release-pubkey.asc',
)


def sign_file(file_path, *, required=False):
    key_id = os.environ.get('MAHO_GPG_KEY_ID', '51C8C6C55603AFF0')
    print(f"Signing {file_path} with GPG key {key_id}...")
    if not shutil.which('gpg'):
        message = "'gpg' command not found; cannot sign release artifact"
        if required:
            raise RuntimeError(message)
        print(f'warning: {message}', file=sys.stderr)
        return False

    sig_file = file_path + '.sig'
    try:
        if os.path.exists(sig_file):
            os.remove(sig_file)
        cmd = [
            'gpg',
            '--detach-sign',
            '--armor',
            '-u',
            key_id,
            '--output',
            sig_file,
            file_path,
        ]
        subprocess.run(cmd, check=True)
        verify_signature(file_path, sig_file)
    except (OSError, subprocess.CalledProcessError) as error:
        message = f'GPG signing or verification failed for {file_path}: {error}'
        if required:
            raise RuntimeError(message) from error
        print(f'warning: {message}', file=sys.stderr)
        return False
    print(f"Signature created and verified: {sig_file}")
    return True


def verify_signature(file_path, sig_file):
    if not os.path.isfile(_RELEASE_PUBLIC_KEY):
        raise RuntimeError(f'committed release public key is missing: {_RELEASE_PUBLIC_KEY}')
    with tempfile.TemporaryDirectory(prefix='maho-gpg-verify-') as gpg_home:
        subprocess.run(
            ['gpg', '--batch', '--homedir', gpg_home, '--import', _RELEASE_PUBLIC_KEY],
            check=True,
        )
        subprocess.run(
            ['gpg', '--batch', '--homedir', gpg_home, '--verify', sig_file, file_path],
            check=True,
        )

def host_platform() -> str:
    if sys.platform == 'darwin':
        return 'mac'
    if sys.platform == 'win32':
        return 'win'
    return 'linux'

def check_prerequisites(fmt):
    if host_platform() != 'linux':
        print("error: Linux host required to build Linux packages", file=sys.stderr)
        sys.exit(1)
        
    if not shutil.which('tar'):
        print("error: 'tar' command is required", file=sys.stderr)
        sys.exit(1)
        
    if fmt in ('deb', 'all') and not shutil.which('dpkg-deb'):
        print("error: 'dpkg-deb' is required to build deb packages.", file=sys.stderr)
        sys.exit(1)
        
    if fmt in ('rpm', 'all') and not shutil.which('rpmbuild'):
        print("error: 'rpmbuild' is required to build rpm packages.", file=sys.stderr)
        sys.exit(1)

def run_build(skip_rust, skip_chromium, release, out_dir):
    cmd = [
        sys.executable,
        _BUILD_MAHO_PY,
        '--platform',
        'linux',
        '--out-dir',
        out_dir,
    ]
    if skip_rust:
        cmd.append('--skip-rust')
    if skip_chromium:
        cmd.append('--skip-chromium')
    if release:
        cmd.extend(['--release', '--force-args-template'])
    
    print(f"Running build wrapper: {' '.join(cmd)}")
    subprocess.run(cmd, check=True)

def copy_browser_files(dest_dir, src_dir):
    os.makedirs(dest_dir, exist_ok=True)

    required_files = [
        'chrome',
        'maho_mail_helper',
        'maho',
        'resources.pak',
        'chrome_100_percent.pak',
        'chrome_200_percent.pak',
        'icudtl.dat',
        'v8_context_snapshot.bin',
        'chrome_crashpad_handler',
        'chrome_management_service',
        'chrome_sandbox',
    ]
    optional_files = [
        'libEGL.so',
        'libGLESv2.so',
        'libvulkan.so.1',
        'libvk_swiftshader.so',
        'vk_swiftshader_icd.json',
    ]
    for filename in required_files:
        src = os.path.join(src_dir, filename)
        if not os.path.isfile(src):
            raise RuntimeError(f'required browser runtime file is missing: {src}')
        destination = 'chrome-sandbox' if filename == 'chrome_sandbox' else filename
        destination_path = os.path.join(dest_dir, destination)
        shutil.copy2(src, destination_path)
        if filename == 'maho_mail_helper':
            os.chmod(destination_path, os.stat(destination_path).st_mode | 0o111)
    for filename in optional_files:
        src = os.path.join(src_dir, filename)
        if os.path.isfile(src):
            shutil.copy2(src, os.path.join(dest_dir, filename))
    for dirname in [
        'locales',
        'MEIPreload',
        'PrivacySandboxAttestationsPreloaded',
    ]:
        src = os.path.join(src_dir, dirname)
        if not os.path.isdir(src):
            raise RuntimeError(f'required browser runtime directory is missing: {src}')
        dest_sub = os.path.join(dest_dir, dirname)
        if os.path.exists(dest_sub):
            shutil.rmtree(dest_sub)
        shutil.copytree(src, dest_sub)
    swiftshader_dir = os.path.join(src_dir, 'swiftshader')
    if os.path.isdir(swiftshader_dir):
        shutil.copytree(swiftshader_dir, os.path.join(dest_dir, 'swiftshader'))


def write_package_launchers(bin_dir, runtime_dir):
    os.makedirs(bin_dir, exist_ok=True)
    cli_launcher = os.path.join(bin_dir, 'maho')
    if os.path.lexists(cli_launcher):
        os.remove(cli_launcher)
    os.symlink(os.path.relpath(os.path.join(runtime_dir, 'maho'), bin_dir), cli_launcher)

    browser_launcher = os.path.join(bin_dir, 'maho-browser')
    with open(browser_launcher, 'w', encoding='utf-8') as file:
        file.write('#!/bin/sh\nexec /usr/lib/maho/chrome "$@"\n')
    os.chmod(browser_launcher, 0o755)


def write_tarball_browser_launcher(runtime_dir):
    launcher_path = os.path.join(runtime_dir, 'maho-browser')
    with open(launcher_path, 'w', encoding='utf-8') as file:
        file.write('#!/bin/sh\nexec "$(dirname "$0")/chrome" "$@"\n')
    os.chmod(launcher_path, 0o755)


def verify_package_contents(command, package_path, expected_paths):
    result = subprocess.run(
        [*command, package_path],
        check=True,
        capture_output=True,
        text=True,
    )
    missing_paths = [path for path in expected_paths if path not in result.stdout]
    if missing_paths:
        raise RuntimeError(
            f'package {package_path} is missing required paths: {", ".join(missing_paths)}'
        )


def package_tarball(version, dry_run, src_dir, release):
    tarball_out = os.path.join(_CHROMIUM_SRC, 'out', 'Linux-tarball')
    
    print(f"Packaging tarball to {tarball_out}...")
    if dry_run:
        return
        
    os.makedirs(tarball_out, exist_ok=True)
    temp_dir = os.path.join(tarball_out, f"maho-{version}")
    if os.path.exists(temp_dir):
        shutil.rmtree(temp_dir)
        
    copy_browser_files(temp_dir, src_dir)
    write_tarball_browser_launcher(temp_dir)
    
    # Copy desktop and icon if available
    branding_dir = os.path.join(_MAHO_CHROMIUM, 'branding')
    desktop_src = os.path.join(branding_dir, 'linux', 'maho.desktop')
    if os.path.exists(desktop_src):
        shutil.copy2(desktop_src, temp_dir)
        
    logo_src = os.path.join(branding_dir, 'product_logo_256.png')
    if os.path.exists(logo_src):
        shutil.copy2(logo_src, os.path.join(temp_dir, 'maho.png'))
        
    tar_name = f"maho-{version}-x86_64.tar.gz"
    tarball_path = os.path.join(tarball_out, tar_name)
    subprocess.run(['tar', '-czf', tarball_path, '-C', tarball_out, f"maho-{version}"], check=True)
    verify_package_contents(
        ['tar', '-tzf'],
        tarball_path,
        [
            f'maho-{version}/chrome',
            f'maho-{version}/maho',
            f'maho-{version}/maho-browser',
        ],
    )
    sign_file(tarball_path, required=release)
    shutil.rmtree(temp_dir)
    print(f"Tarball created successfully: {tarball_path}")

def package_deb(version, dry_run, src_dir, release):
    deb_out = os.path.join(_CHROMIUM_SRC, 'out', 'Linux-deb')
    
    print(f"Packaging deb to {deb_out}...")
    if dry_run:
        return
        
    os.makedirs(deb_out, exist_ok=True)
    build_dir = os.path.join(deb_out, 'build')
    if os.path.exists(build_dir):
        shutil.rmtree(build_dir)
        
    # Create Debian layout
    os.makedirs(os.path.join(build_dir, 'DEBIAN'))
    os.makedirs(os.path.join(build_dir, 'usr', 'bin'))
    os.makedirs(os.path.join(build_dir, 'usr', 'lib', 'maho'))
    os.makedirs(os.path.join(build_dir, 'usr', 'share', 'applications'))
    os.makedirs(os.path.join(build_dir, 'usr', 'share', 'pixmaps'))
    os.makedirs(os.path.join(build_dir, 'etc', 'apparmor.d'))
    
    # Copy browser binaries
    copy_browser_files(os.path.join(build_dir, 'usr', 'lib', 'maho'), src_dir)
    
    write_package_launchers(
        os.path.join(build_dir, 'usr', 'bin'),
        os.path.join(build_dir, 'usr', 'lib', 'maho'),
    )
    
    # Copy desktop file and logo
    branding_dir = os.path.join(_MAHO_CHROMIUM, 'branding')
    shutil.copy2(os.path.join(branding_dir, 'linux', 'maho.desktop'),
                 os.path.join(build_dir, 'usr', 'share', 'applications', 'maho.desktop'))
    shutil.copy2(os.path.join(branding_dir, 'product_logo_256.png'),
                 os.path.join(build_dir, 'usr/share/pixmaps/maho.png'))
                 
    # Create AppArmor profile
    apparmor_content = """abi <abi/4.0>,
include <tunables/global>

profile maho /usr/lib/maho/chrome flags=(unconfined) {
  userns,
  # Site-specific additions and overrides.
  include if exists <local/maho>
}
"""
    with open(os.path.join(build_dir, 'etc', 'apparmor.d', 'maho'), 'w') as f:
        f.write(apparmor_content)

    # Create postinst script
    postinst_content = """#!/bin/sh
set -e

# Set SUID permissions for the sandbox helper
if [ -f "/usr/lib/maho/chrome-sandbox" ]; then
  chown root:root "/usr/lib/maho/chrome-sandbox"
  chmod 4755 "/usr/lib/maho/chrome-sandbox"
fi

# AppArmor profile load
if [ -f "/etc/apparmor.d/maho" ] && command -v apparmor_parser >/dev/null 2>&1; then
  apparmor_parser -r /etc/apparmor.d/maho || true
fi
"""
    postinst_path = os.path.join(build_dir, 'DEBIAN', 'postinst')
    with open(postinst_path, 'w') as f:
        f.write(postinst_content)
    os.chmod(postinst_path, 0o755)

    # Create control file
    control_content = f"""Package: maho
Version: {version}
Section: web
Priority: optional
Architecture: amd64
Depends: libnss3, libnspr4, libgtk-3-0t64 | libgtk-3-0, libgbm1, libasound2t64 | libasound2, libcups2t64 | libcups2, libxss1, libpci3, libdrm2, libxkbcommon0, libatk1.0-0t64 | libatk1.0-0, libatk-bridge2.0-0t64 | libatk-bridge2.0-0, libatspi2.0-0t64 | libatspi2.0-0, libcairo2, libpango-1.0-0, libx11-6, libxcb1, libxcomposite1, libxcursor1, libxdamage1, libxext6, libxfixes3, libxi6, libxrandr2, libxrender1, libxtst6, libexpat1, fonts-liberation
Maintainer: Maho Browser Releases <releases@maho.app>
Description: Maho Browser — AI-native web browser
"""
    with open(os.path.join(build_dir, 'DEBIAN', 'control'), 'w') as f:
        f.write(control_content)
        
    # Build package
    deb_file = os.path.join(deb_out, f"maho_{version}_amd64.deb")
    subprocess.run(['dpkg-deb', '--build', build_dir, deb_file], check=True)
    verify_package_contents(
        ['dpkg-deb', '--contents'],
        deb_file,
        ['usr/bin/maho', 'usr/bin/maho-browser', 'usr/lib/maho/maho', 'usr/lib/maho/chrome'],
    )
    shutil.rmtree(build_dir)
    print(f"Deb package created successfully: {deb_file}")
    sign_file(deb_file, required=release)

def package_rpm(version, dry_run, src_dir, release):
    rpm_out = os.path.join(_CHROMIUM_SRC, 'out', 'Linux-rpm')
    
    print(f"Packaging rpm to {rpm_out}...")
    if dry_run:
        return
        
    rpmbuild_dir = os.path.join(rpm_out, 'rpmbuild')
    if os.path.exists(rpmbuild_dir):
        shutil.rmtree(rpmbuild_dir)
        
    # Create rpmbuild layout
    for subdir in ['SPECS', 'SOURCES', 'BUILD', 'RPMS', 'SRPMS']:
        os.makedirs(os.path.join(rpmbuild_dir, subdir))
        
    # Copy browser files to SOURCES
    src_tar_dir = os.path.join(rpmbuild_dir, 'SOURCES', f"maho-{version}")
    copy_browser_files(src_tar_dir, src_dir)
    
    # Create launcher
    launcher_path = os.path.join(src_tar_dir, 'maho-browser.sh')
    with open(launcher_path, 'w') as f:
        f.write('#!/bin/sh\nexec /usr/lib/maho/chrome "$@"\n')
    os.chmod(launcher_path, 0o755)
    
    # Copy desktop and icon
    branding_dir = os.path.join(_MAHO_CHROMIUM, 'branding')
    shutil.copy2(os.path.join(branding_dir, 'linux', 'maho.desktop'), os.path.join(src_tar_dir, 'maho.desktop'))
    shutil.copy2(os.path.join(branding_dir, 'product_logo_256.png'), os.path.join(src_tar_dir, 'maho.png'))
    
    # Create tarball in SOURCES
    subprocess.run(['tar', '-czf', os.path.join(rpmbuild_dir, 'SOURCES', f"maho-{version}.tar.gz"),
                    '-C', os.path.join(rpmbuild_dir, 'SOURCES'), f"maho-{version}"], check=True)
    shutil.rmtree(src_tar_dir)
    
    # Write spec file
    spec_content = f"""%global debug_package %{{nil}}
Name:           maho
Version:        {version}
Release:        1
Summary:        Maho Browser — AI-native web browser
License:        Proprietary
URL:            https://maho.app
Source0:        %{{name}}-%{{version}}.tar.gz

Requires:       nss, nspr, gtk3, mesa-libgbm, alsa-lib, cups-libs, libXScrnSaver, pciutils-libs, libdrm, libxkbcommon, at-spi2-atk, at-spi2-core, cairo, pango, libX11, libxcb, libXcomposite, libXcursor, libXdamage, libXext, libXfixes, libXi, libXrandr, libXrender, libXtst, expat, liberation-fonts

%description
Maho Browser — AI-native web browser.

%prep
%setup -q

%build
# Precompiled, no-op

%install
mkdir -p %{{buildroot}}/usr/lib/maho
mkdir -p %{{buildroot}}/usr/bin
mkdir -p %{{buildroot}}/usr/share/applications
mkdir -p %{{buildroot}}/usr/share/pixmaps
cp -r * %{{buildroot}}/usr/lib/maho/
ln -s ../lib/maho/maho %{{buildroot}}/usr/bin/maho
mv %{{buildroot}}/usr/lib/maho/maho-browser.sh %{{buildroot}}/usr/bin/maho-browser
mv %{{buildroot}}/usr/lib/maho/maho.desktop %{{buildroot}}/usr/share/applications/maho.desktop
mv %{{buildroot}}/usr/lib/maho/maho.png %{{buildroot}}/usr/share/pixmaps/maho.png

%post
if [ -f "/usr/lib/maho/chrome-sandbox" ]; then
  chown root:root "/usr/lib/maho/chrome-sandbox"
  chmod 4755 "/usr/lib/maho/chrome-sandbox"
fi

%files
/usr/lib/maho/
/usr/bin/maho
/usr/bin/maho-browser
/usr/share/applications/maho.desktop
/usr/share/pixmaps/maho.png
"""
    spec_file = os.path.join(rpmbuild_dir, 'SPECS', 'maho.spec')
    with open(spec_file, 'w') as f:
        f.write(spec_content)
        
    # Run rpmbuild
    subprocess.run(['rpmbuild', '--define', f"_topdir {rpmbuild_dir}", '-bb', spec_file], check=True)
    print(f"RPM packages created successfully in {os.path.join(rpmbuild_dir, 'RPMS')}")

    # Sign any RPM files built
    rpms_dir = os.path.join(rpmbuild_dir, 'RPMS')
    if os.path.exists(rpms_dir):
        for root, dirs, files in os.walk(rpms_dir):
            for file in files:
                if file.endswith('.rpm'):
                    rpm_path = os.path.join(root, file)
                    verify_package_contents(
                        ['rpm', '--query', '--list', '--package'],
                        rpm_path,
                        ['/usr/bin/maho', '/usr/bin/maho-browser', '/usr/lib/maho/maho', '/usr/lib/maho/chrome'],
                    )
                    sign_file(rpm_path, required=release)

def main():
    parser = argparse.ArgumentParser(description="Package Maho Browser for Linux.")
    parser.add_argument('--format', choices=('tarball', 'deb', 'rpm', 'all'), required=True,
                        help="Packaging formats to build.")
    parser.add_argument('--skip-rust', action='store_true', help="Skip the Rust prebuilt step.")
    parser.add_argument('--skip-chromium', action='store_true', help="Skip the Chromium build step.")
    parser.add_argument('--dry-run', action='store_true', help="Dry run: print steps without executing packaging.")
    parser.add_argument('--check', action='store_true', help="Check packaging prerequisites and exit.")
    parser.add_argument('--release', action='store_true', help="Build in production release mode.")
    parser.add_argument(
        '--out-dir',
        default='out/Default',
        help='Chromium output directory relative to chromium/src.',
    )
    args = parser.parse_args()
    
    if args.check:
        check_prerequisites(args.format)
        print("Prerequisites check passed.")
        return 0
        
    check_prerequisites(args.format)
    
    # 1. Run Chromium build if requested
    if not (args.skip_rust and args.skip_chromium):
        run_build(args.skip_rust, args.skip_chromium, args.release, args.out_dir)
        
    # Read version
    version = "1.0.0"
    version_file = os.path.join(_WORKSPACE_ROOT, 'maho', 'version.txt')
    if os.path.isfile(version_file):
        with open(version_file, 'r') as f:
            version = f.read().strip()
            
    # 2. Package
    formats = ['tarball', 'deb', 'rpm'] if args.format == 'all' else [args.format]
    src_dir = os.path.join(_CHROMIUM_SRC, args.out_dir)
    
    for fmt in formats:
        if fmt == 'tarball':
            package_tarball(version, args.dry_run, src_dir, args.release)
        elif fmt == 'deb':
            package_deb(version, args.dry_run, src_dir, args.release)
        elif fmt == 'rpm':
            package_rpm(version, args.dry_run, src_dir, args.release)
            
    return 0

if __name__ == '__main__':
    sys.exit(main())
