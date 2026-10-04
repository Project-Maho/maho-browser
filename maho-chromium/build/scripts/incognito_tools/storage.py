import sqlite3

def get_sqlite_schema(db_path):
    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()
    cursor.execute("SELECT name, sql FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'")
    schema = cursor.fetchall()
    conn.close()
    return schema

def get_sqlite_rows(db_path, table_name):
    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()
    cursor.execute(f"PRAGMA table_info({table_name})")
    cols = [col[1] for col in cursor.fetchall()]
    cols_str = ", ".join(cols)
    cursor.execute(f"SELECT {cols_str} FROM {table_name}")
    rows = cursor.fetchall()
    conn.close()
    return cols, rows
