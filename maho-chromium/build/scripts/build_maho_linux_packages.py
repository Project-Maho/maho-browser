#!/usr/bin/env python3
"""Build and package Maho Browser for Linux distributions."""

import argparse
import os
import shutil
import subprocess
import sys

_SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
_WORKSPACE_ROOT = os.path.normpath(os.path.join(_SCRIPT_DIR, '..', '..', '..'))
_CHROMIUM_SRC = os.path.join(_WORKSPACE_ROOT, 'chromium', 'src')
_MAHO_CHROMIUM = os.path.join(_WORKSPACE_ROOT, 'maho-chromium')
_BUILD_MAHO_PY = os.path.join(_SCRIPT_DIR, 'build_maho.py')

def sign_file(file_path):
    key_id = '51C8C6C55603AFF0'
    print(f"Signing {file_path} with GPG key {key_id}...")
    try:
        if not shutil.which('gpg'):
            print("warning: 'gpg' command not found, skipping signature generation", file=sys.stderr)
            return False
            
        sig_file = file_path + '.sig'
        if os.path.exists(sig_file):
            os.remove(sig_file)
            
        cmd = ['gpg', '--detach-sign', '--armor', '-u', key_id, '--output', sig_file, file_path]
        subprocess.run(cmd, check=True)
        print(f"Signature created: {sig_file}")
        return True
    except Exception as e:
        print(f"warning: GPG signing failed: {e}", file=sys.stderr)
        return False

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

def run_build(skip_rust, skip_chromium, release):
    cmd = [sys.executable, _BUILD_MAHO_PY, '--platform', 'linux']
    if skip_rust:
        cmd.append('--skip-rust')
    if skip_chromium:
        cmd.append('--skip-chromium')
    if release:
        cmd.append('--release')
    
    print(f"Running build wrapper: {' '.join(cmd)}")
    subprocess.run(cmd, check=True)

def copy_browser_files(dest_dir, src_dir):
    os.makedirs(dest_dir, exist_ok=True)
    
    # Files to copy from build output
    files_to_copy = [
        'chrome', 'resources.pak', 'chrome_100_percent.pak', 'chrome_200_percent.pak',
        'icudtl.dat', 'v8_context_snapshot.bin', 'chrome_crashpad_handler'
    ]
    
    for filename in files_to_copy:
        src = os.path.join(src_dir, filename)
        if os.path.exists(src):
            shutil.copy2(src, os.path.join(dest_dir, filename))
            
    # Copy directories if present
    for dirname in ['locales', 'swiftshader', 'MEIPreload']:
        src = os.path.join(src_dir, dirname)
        if os.path.isdir(src):
            dest_sub = os.path.join(dest_dir, dirname)
            if os.path.exists(dest_sub):
                shutil.rmtree(dest_sub)
            shutil.copytree(src, dest_sub)

def package_tarball(version, dry_run):
    src_dir = os.path.join(_CHROMIUM_SRC, 'out', 'Default')
    tarball_out = os.path.join(_CHROMIUM_SRC, 'out', 'Linux-tarball')
    
    print(f"Packaging tarball to {tarball_out}...")
    if dry_run:
        return
        
    os.makedirs(tarball_out, exist_ok=True)
    temp_dir = os.path.join(tarball_out, f"maho-{version}")
    if os.path.exists(temp_dir):
        shutil.rmtree(temp_dir)
        
    copy_browser_files(temp_dir, src_dir)
    
    # Copy desktop and icon if available
    branding_dir = os.path.join(_MAHO_CHROMIUM, 'branding')
    desktop_src = os.path.join(branding_dir, 'linux', 'maho.desktop')
    if os.path.exists(desktop_src):
        shutil.copy2(desktop_src, temp_dir)
        
    logo_src = os.path.join(branding_dir, 'product_logo_256.png')
    if os.path.exists(logo_src):
        shutil.copy2(logo_src, os.path.join(temp_dir, 'maho.png'))
        
    tar_name = f"maho-{version}-x86_64.tar.gz"
    subprocess.run(['tar', '-czf', os.path.join(tarball_out, tar_name), '-C', tarball_out, f"maho-{version}"], check=True)
    shutil.rmtree(temp_dir)
    print(f"Tarball created successfully: {os.path.join(tarball_out, tar_name)}")

def package_deb(version, dry_run):
    src_dir = os.path.join(_CHROMIUM_SRC, 'out', 'Default')
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
    
    # Copy browser binaries
    copy_browser_files(os.path.join(build_dir, 'usr', 'lib', 'maho'), src_dir)
    
    # Create CLI launcher script
    launcher_path = os.path.join(build_dir, 'usr', 'bin', 'maho')
    with open(launcher_path, 'w') as f:
        f.write('#!/bin/sh\nexec /usr/lib/maho/chrome "$@"\n')
    os.chmod(launcher_path, 0o755)
    
    # Copy desktop file and logo
    branding_dir = os.path.join(_MAHO_CHROMIUM, 'branding')
    shutil.copy2(os.path.join(branding_dir, 'linux', 'maho.desktop'),
                 os.path.join(build_dir, 'usr', 'share', 'applications', 'maho.desktop'))
    shutil.copy2(os.path.join(branding_dir, 'product_logo_256.png'),
                 os.path.join(build_dir, 'usr/share/pixmaps/maho.png'))
                 
    # Create control file
    control_content = f"""Package: maho
Version: {version}
Section: web
Priority: optional
Architecture: amd64
Depends: libgtk-3-0, libdbus-1-3, libnss3, libatk1.0-0
Maintainer: Maho Browser Releases <releases@maho.app>
Description: Maho Browser — AI-native web browser
"""
    with open(os.path.join(build_dir, 'DEBIAN', 'control'), 'w') as f:
        f.write(control_content)
        
    # Build package
    deb_file = os.path.join(deb_out, f"maho_{version}_amd64.deb")
    subprocess.run(['dpkg-deb', '--build', build_dir, deb_file], check=True)
    shutil.rmtree(build_dir)
    print(f"Deb package created successfully: {deb_file}")
    sign_file(deb_file)

def package_rpm(version, dry_run):
    src_dir = os.path.join(_CHROMIUM_SRC, 'out', 'Default')
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
    launcher_path = os.path.join(src_tar_dir, 'maho.sh')
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
    spec_content = f"""Name:           maho
Version:        {version}
Release:        1
Summary:        Maho Browser — AI-native web browser
License:        Proprietary
URL:            https://maho.app
Source0:        %{{name}}-%{{version}}.tar.gz

Requires:       gtk3, dbus-libs, nss

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
mv %{{buildroot}}/usr/lib/maho/maho.sh %{{buildroot}}/usr/bin/maho
mv %{{buildroot}}/usr/lib/maho/maho.desktop %{{buildroot}}/usr/share/applications/maho.desktop
mv %{{buildroot}}/usr/lib/maho/maho.png %{{buildroot}}/usr/share/pixmaps/maho.png

%files
/usr/lib/maho/
/usr/bin/maho
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
                    sign_file(rpm_path)

def main():
    parser = argparse.ArgumentParser(description="Package Maho Browser for Linux.")
    parser.add_argument('--format', choices=('tarball', 'deb', 'rpm', 'all'), required=True,
                        help="Packaging formats to build.")
    parser.add_argument('--skip-rust', action='store_true', help="Skip the Rust prebuilt step.")
    parser.add_argument('--skip-chromium', action='store_true', help="Skip the Chromium build step.")
    parser.add_argument('--dry-run', action='store_true', help="Dry run: print steps without executing packaging.")
    parser.add_argument('--check', action='store_true', help="Check packaging prerequisites and exit.")
    parser.add_argument('--release', action='store_true', help="Build in production release mode.")
    args = parser.parse_args()
    
    if args.check:
        check_prerequisites(args.format)
        print("Prerequisites check passed.")
        return 0
        
    check_prerequisites(args.format)
    
    # 1. Run Chromium build if requested
    if not (args.skip_rust and args.skip_chromium):
        run_build(args.skip_rust, args.skip_chromium, args.release)
        
    # Read version
    version = "1.0.0"
    version_file = os.path.join(_WORKSPACE_ROOT, 'maho', 'version.txt')
    if os.path.isfile(version_file):
        with open(version_file, 'r') as f:
            version = f.read().strip()
            
    # 2. Package
    formats = ['tarball', 'deb', 'rpm'] if args.format == 'all' else [args.format]
    
    for fmt in formats:
        if fmt == 'tarball':
            package_tarball(version, args.dry_run)
        elif fmt == 'deb':
            package_deb(version, args.dry_run)
        elif fmt == 'rpm':
            package_rpm(version, args.dry_run)
            
    return 0

if __name__ == '__main__':
    sys.exit(main())
