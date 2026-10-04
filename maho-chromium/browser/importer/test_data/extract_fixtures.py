#!/usr/bin/env python3
import os
import sys
import argparse
import shutil
import sqlite3
import json
import plistlib
import re

# Resolve absolute paths
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REAL_DIR = os.path.join(SCRIPT_DIR, 'real')

def ensure_real_dir():
    os.makedirs(REAL_DIR, exist_ok=True)

def sanitize_string(s):
    if not isinstance(s, str):
        return s
    # Strip email-like patterns
    s = re.sub(r'[\w\.-]+@[\w\.-]+\.\w+', 'user@example.com', s)
    # Strip potential username parts from URLs or local paths
    s = re.sub(r'/Users/[\w\.-]+', '/Users/sanitized_user', s)
    # Replace domains with example.com where applicable
    s = re.sub(r'https?://(?:www\.)?[\w\.-]+\.\w+', 'https://example.com', s)
    return s

def sanitize_sqlite(src_path, dst_path, query_updates):
    try:
        shutil.copyfile(src_path, dst_path)
        conn = sqlite3.connect(dst_path)
        cursor = conn.cursor()
        for query, params in query_updates:
            cursor.execute(query, params)
        conn.commit()
        conn.close()
        return True
    except Exception as e:
        print(f"Failed to sanitize SQLite database {src_path}: {e}")
        if os.path.exists(dst_path):
            os.remove(dst_path)
        return False

def write_mock_chrome_bookmarks():
    ensure_real_dir()
    bookmarks_data = {
        "roots": {
            "bookmark_bar": {
                "children": [
                    {"name": "Google", "type": "url", "url": "https://example.com"},
                    {"name": "GitHub", "type": "url", "url": "https://example.com/github"}
                ],
                "name": "Bookmarks Bar",
                "type": "folder"
            }
        },
        "version": 1
    }
    with open(os.path.join(REAL_DIR, 'chrome_bookmarks.json'), 'w') as f:
        json.dump(bookmarks_data, f, indent=2)

def write_mock_chrome_login_data():
    ensure_real_dir()
    db_path = os.path.join(REAL_DIR, 'chrome_login_data.db')
    if os.path.exists(db_path):
        os.remove(db_path)
    conn = sqlite3.connect(db_path)
    conn.execute("CREATE TABLE logins (origin_url TEXT, action_url TEXT, username_value TEXT, password_value BLOB)")
    conn.execute("INSERT INTO logins VALUES ('https://example.com', 'https://example.com', 'user@example.com', x'01020304')")
    conn.commit()
    conn.close()

def extract_chrome(dry_run):
    chrome_path = os.path.expanduser('~/Library/Application Support/Google/Chrome')
    print(f"Checking Chrome path: {chrome_path}")
    profiles = []
    if os.path.exists(chrome_path):
        for item in os.listdir(chrome_path):
            if item in ['Default'] or item.startswith('Profile '):
                p_path = os.path.join(chrome_path, item)
                if os.path.isdir(p_path):
                    profiles.append(item)
    
    if not profiles:
        print("No real Chrome profiles found. Generating mock Chrome fixtures.")
        if not dry_run:
            write_mock_chrome_bookmarks()
            write_mock_chrome_login_data()
        return

    print(f"Found Chrome profiles: {profiles}")
    if dry_run:
        return

    ensure_real_dir()
    p_path = os.path.join(chrome_path, profiles[0])
    bookmarks_src = os.path.join(p_path, 'Bookmarks')
    login_src = os.path.join(p_path, 'Login Data')
    
    bookmarks_success = False
    if os.path.exists(bookmarks_src):
        try:
            with open(bookmarks_src, 'r', errors='ignore') as f:
                data = json.load(f)
            def sanitize_json(obj):
                if isinstance(obj, dict):
                    return {k: sanitize_json(v) for k, v in obj.items()}
                elif isinstance(obj, list):
                    return [sanitize_json(x) for x in obj]
                else:
                    return sanitize_string(obj)
            sanitized = sanitize_json(data)
            with open(os.path.join(REAL_DIR, 'chrome_bookmarks.json'), 'w') as f:
                json.dump(sanitized, f, indent=2)
            bookmarks_success = True
        except Exception as e:
            print(f"Error reading Chrome bookmarks: {e}")
            
    if not bookmarks_success:
        write_mock_chrome_bookmarks()

    login_success = False
    if os.path.exists(login_src):
        res = sanitize_sqlite(login_src, os.path.join(REAL_DIR, 'chrome_login_data.db'), [
            ("UPDATE logins SET origin_url = 'https://example.com', action_url = 'https://example.com', username_value = 'user@example.com'", ())
        ])
        if res:
            login_success = True

    if not login_success:
        write_mock_chrome_login_data()

def write_mock_firefox_logins():
    ensure_real_dir()
    logins_data = {
        "nextId": 2,
        "logins": [
            {
                "hostname": "https://example.com",
                "username": "user@example.com",
                "encryptedUsername": "username_blob",
                "encryptedPassword": "password_blob"
            }
        ]
    }
    with open(os.path.join(REAL_DIR, 'firefox_logins.json'), 'w') as f:
        json.dump(logins_data, f, indent=2)

def write_mock_firefox_places():
    ensure_real_dir()
    db_path = os.path.join(REAL_DIR, 'firefox_places.sqlite')
    if os.path.exists(db_path):
        os.remove(db_path)
    conn = sqlite3.connect(db_path)
    conn.execute("CREATE TABLE moz_places (id INTEGER PRIMARY KEY, url TEXT, title TEXT)")
    conn.execute("INSERT INTO moz_places VALUES (1, 'https://example.com', 'Example Site')")
    conn.commit()
    conn.close()

def extract_firefox(dry_run):
    firefox_path = os.path.expanduser('~/Library/Application Support/Firefox/Profiles')
    print(f"Checking Firefox path: {firefox_path}")
    profiles = []
    if os.path.exists(firefox_path):
        for item in os.listdir(firefox_path):
            p_path = os.path.join(firefox_path, item)
            if os.path.isdir(p_path) and os.path.exists(os.path.join(p_path, 'places.sqlite')):
                profiles.append(item)

    if not profiles:
        print("No real Firefox profiles found. Generating mock Firefox fixtures.")
        if not dry_run:
            write_mock_firefox_logins()
            write_mock_firefox_places()
        return

    print(f"Found Firefox profiles: {profiles}")
    if dry_run:
        return

    ensure_real_dir()
    p_path = os.path.join(firefox_path, profiles[0])
    logins_src = os.path.join(p_path, 'logins.json')
    places_src = os.path.join(p_path, 'places.sqlite')
    
    logins_success = False
    if os.path.exists(logins_src):
        try:
            with open(logins_src, 'r') as f:
                data = json.load(f)
            if "logins" in data:
                for login in data["logins"]:
                    if "hostname" in login:
                        login["hostname"] = "https://example.com"
                    if "username" in login:
                        login["username"] = "user@example.com"
            with open(os.path.join(REAL_DIR, 'firefox_logins.json'), 'w') as f:
                json.dump(data, f, indent=2)
            logins_success = True
        except Exception as e:
            print(f"Error reading Firefox logins.json: {e}")

    if not logins_success:
        write_mock_firefox_logins()

    places_success = False
    if os.path.exists(places_src):
        res = sanitize_sqlite(places_src, os.path.join(REAL_DIR, 'firefox_places.sqlite'), [
            ("UPDATE moz_places SET url = 'https://example.com', title = 'Sanitized Site'", ())
        ])
        if res:
            places_success = True

    if not places_success:
        write_mock_firefox_places()

def write_mock_zen():
    ensure_real_dir()
    workspaces_data = {
        "workspaces": [
            {"id": "work-space", "name": "Work", "userContextId": 1},
            {"id": "personal-space", "name": "Personal", "userContextId": 2}
        ]
    }
    with open(os.path.join(REAL_DIR, 'zen_workspaces.json'), 'w') as f:
        json.dump(workspaces_data, f, indent=2)

def extract_zen(dry_run):
    zen_path = os.path.expanduser('~/Library/Application Support/zen/Profiles')
    print(f"Checking Zen path: {zen_path}")
    profiles = []
    if os.path.exists(zen_path):
        for item in os.listdir(zen_path):
            p_path = os.path.join(zen_path, item)
            if os.path.isdir(p_path):
                profiles.append(item)

    if not profiles:
        print("No real Zen profiles found. Generating mock Zen fixtures.")
        if not dry_run:
            write_mock_zen()
        return

    print(f"Found Zen profiles: {profiles}")
    if dry_run:
        return

    ensure_real_dir()
    p_path = os.path.join(zen_path, profiles[0])
    workspaces_src = os.path.join(p_path, 'workspaces.json')
    zen_success = False
    if os.path.exists(workspaces_src):
        try:
            with open(workspaces_src, 'r') as f:
                data = json.load(f)
            if "workspaces" in data:
                for ws in data["workspaces"]:
                    if "name" in ws:
                        ws["name"] = sanitize_string(ws["name"])
            with open(os.path.join(REAL_DIR, 'zen_workspaces.json'), 'w') as f:
                json.dump(data, f, indent=2)
            zen_success = True
        except Exception as e:
            print(f"Error reading Zen workspaces.json: {e}")

    if not zen_success:
        print("Could not read real Zen workspaces. Generating mock Zen fixtures.")
        write_mock_zen()

def write_mock_arc():
    ensure_real_dir()
    mock_plist = {
        "sidebar": {
            "containers": [
                {
                    "items": [
                        {"title": "Google Search", "url": "https://example.com"},
                        {"title": "GitHub Repo", "url": "https://example.com/github"}
                    ]
                }
            ]
        }
    }
    with open(os.path.join(REAL_DIR, 'arc_sidebar.plist'), 'wb') as f:
        plistlib.dump(mock_plist, f)

def extract_arc(dry_run):
    arc_path = os.path.expanduser('~/Library/Application Support/Arc')
    print(f"Checking Arc path: {arc_path}")
    sidebar_src = os.path.join(arc_path, 'StorableSidebar.plist')
    
    if not os.path.exists(sidebar_src):
        print("No real Arc sidebar plist found. Generating mock Arc fixtures.")
        if not dry_run:
            write_mock_arc()
        return

    print("Found Arc sidebar plist.")
    if dry_run:
        return

    ensure_real_dir()
    try:
        with open(sidebar_src, 'rb') as f:
            data = plistlib.load(f)
        def sanitize_plist_node(node):
            if isinstance(node, dict):
                return {k: sanitize_plist_node(v) for k, v in node.items()}
            elif isinstance(node, list):
                return [sanitize_plist_node(x) for x in node]
            else:
                return sanitize_string(node)
        sanitized = sanitize_plist_node(data)
        with open(os.path.join(REAL_DIR, 'arc_sidebar.plist'), 'wb') as f:
            plistlib.dump(sanitized, f)
    except Exception as e:
        print(f"Error reading Arc sidebar.plist: {e}")
        write_mock_arc()

def write_mock_safari():
    ensure_real_dir()
    mock_plist = {
        "Children": [
            {
                "Title": "Safari Bookmark Bar",
                "Children": [
                    {"URIDictionary": {"title": "Example"}, "URLString": "https://example.com"}
                ]
            }
        ]
    }
    with open(os.path.join(REAL_DIR, 'safari_bookmarks.plist'), 'wb') as f:
        plistlib.dump(mock_plist, f)

def extract_safari(dry_run):
    safari_path = os.path.expanduser('~/Library/Safari')
    print(f"Checking Safari path: {safari_path}")
    bookmarks_src = os.path.join(safari_path, 'Bookmarks.plist')

    if not os.path.exists(bookmarks_src):
        print("No real Safari Bookmarks.plist found. Generating mock Safari fixtures.")
        if not dry_run:
            write_mock_safari()
        return

    print("Found Safari Bookmarks plist.")
    if dry_run:
        return

    ensure_real_dir()
    try:
        with open(bookmarks_src, 'rb') as f:
            data = plistlib.load(f)
        def sanitize_plist_node(node):
            if isinstance(node, dict):
                return {k: sanitize_plist_node(v) for k, v in node.items()}
            elif isinstance(node, list):
                return [sanitize_plist_node(x) for x in node]
            else:
                return sanitize_string(node)
        sanitized = sanitize_plist_node(data)
        with open(os.path.join(REAL_DIR, 'safari_bookmarks.plist'), 'wb') as f:
            plistlib.dump(sanitized, f)
    except Exception as e:
        print(f"Error reading Safari Bookmarks.plist: {e}")
        write_mock_safari()

def main():
    parser = argparse.ArgumentParser(description="Extract and sanitize real browser profile data.")
    parser.add_argument('--dry-run', action='store_true', help="Check paths and profiles without writing output files.")
    args = parser.parse_args()

    extract_chrome(args.dry_run)
    extract_firefox(args.dry_run)
    extract_zen(args.dry_run)
    extract_arc(args.dry_run)
    extract_safari(args.dry_run)
    
    print("Done extraction process.")

if __name__ == '__main__':
    main()
