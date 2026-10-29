import os
import re

ENGINE_SRC = r"C:\Users\nanodev\.gemini\antigravity\scratch\NanoDecompiler\resources\engine_cpp\src"

def clean_file(filename, patterns):
    path = os.path.join(ENGINE_SRC, filename)
    with open(path, "r", encoding="utf-8") as f:
        content = f.read()
    orig = content
    for pat in patterns:
        content = re.sub(pat, "", content, flags=re.MULTILINE)
    if content != orig:
        with open(path, "w", encoding="utf-8") as f:
            f.write(content)
        print(f"Cleaned {filename}")
    else:
        print(f"No changes in {filename}")

# 1. structure.cpp: remove std::cerr << " [PASS...
clean_file("structure.cpp", [
    r'^\s*std::cerr\s*<<\s*" \[PASS[^;]*;\s*$\n',
    r'^\s*std::cerr\s*<<\s*" \[PASS-ITER[^;]*;\s*$\n',
])

# 2. engine.cpp: remove std::cerr << " [DBG:...
clean_file("engine.cpp", [
    r'^\s*std::cerr\s*<<\s*" \[DBG:[^;]*;\s*$\n',
])

# 3. render_class.cpp: remove std::cerr << " [Method]...
clean_file("render_class.cpp", [
    r'^\s*std::cerr\s*<<\s*" \[Method\][^;]*;\s*$\n',
])

print("Engine debug prints completely wiped!")
