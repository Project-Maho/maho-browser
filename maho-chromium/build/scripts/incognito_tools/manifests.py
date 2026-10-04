import os

def parse_owned_product_paths(filepath):
    paths = []
    if not os.path.exists(filepath):
        return paths
    with open(filepath, 'r') as f:
        for line in f:
            parts = line.strip().split('|')
            if len(parts) >= 3:
                paths.append({
                    'task': parts[0],
                    'mode': parts[1],
                    'path': parts[2]
                })
    return paths

def parse_owned_evidence_prefixes(filepath):
    prefixes = []
    if not os.path.exists(filepath):
        return prefixes
    with open(filepath, 'r') as f:
        for line in f:
            p = line.strip()
            if p:
                prefixes.append(p)
    return prefixes
