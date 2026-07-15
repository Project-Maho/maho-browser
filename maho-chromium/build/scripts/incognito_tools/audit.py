import difflib

def verify_diff(pre_content, post_content):
    diff_lines = list(difflib.unified_diff(
        pre_content.splitlines(),
        post_content.splitlines(),
        n=0
    ))

    errors = []
    for line in diff_lines:
        if line.startswith('+') and not line.startswith('+++'):
            added_content = line[1:]
            if added_content.endswith(' ') or added_content.endswith('\t'):
                errors.append(f"Trailing whitespace: {line}")
            if any(marker in added_content for marker in ['<<<<<<<', '=======', '>>>>>>>']):
                errors.append(f"Conflict marker found: {line}")
    return errors, diff_lines
