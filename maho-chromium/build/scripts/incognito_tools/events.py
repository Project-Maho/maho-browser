import os

def parse_fs_events(filepath):
    events = []
    if not os.path.exists(filepath):
        return events
    with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            parts = line.strip().split('|')
            if len(parts) >= 2:
                events.append((parts[0], parts[1]))
    return events
