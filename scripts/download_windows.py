import hashlib
import os
import sys
from pathlib import Path

import requests

from _gh_auth import github_headers

# Папка назначения: раньше была зашита личная папка автора
# (C:\Users\nanodev\Desktop\NanoDecompiler) - с чужим именем пользователя в
# публичном репозитории и сломанная на любой другой машине. Теперь Desktop
# текущего пользователя либо путь из первого аргумента.
desktop_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path.home() / "Desktop" / "NanoDecompiler"
desktop_dir.mkdir(parents=True, exist_ok=True)

url = "https://api.github.com/repos/NanoDev1488/NanoDecompiler/releases/latest"
headers = github_headers()

print(f"Fetching latest release from {url}...")
resp = requests.get(url, headers=headers, timeout=20)
resp.raise_for_status()
data = resp.json()

tag = data.get("tag_name", "unknown")
print(f"Latest release: {tag}")

assets = data.get("assets", [])
windows_assets = [a for a in assets if "Windows" in a["name"] or a["name"].endswith(".exe")]

# SHA256 из checksums.json релиза - проверяем каждый скачанный файл.
checksums = {}
checksums_asset = next((a for a in assets if a["name"] == "checksums.json"), None)
if checksums_asset:
    cr = requests.get(checksums_asset["browser_download_url"], headers=headers, timeout=20)
    cr.raise_for_status()
    checksums = cr.json()
else:
    print("ВНИМАНИЕ: в релизе нет checksums.json - целостность файлов проверить нельзя.")

print(f"Found {len(windows_assets)} Windows assets:")
for a in windows_assets:
    print(f" - {a['name']} ({a['size']} bytes)")

for a in windows_assets:
    # basename: имя ассета не должно уводить запись за пределы папки назначения
    fname = os.path.basename(a["name"])
    if not fname or fname in (".", ".."):
        print(f"Skip suspicious asset name: {a['name']!r}")
        continue
    target_path = desktop_dir / fname
    download_url = a["browser_download_url"]
    print(f"Downloading {fname} to {target_path}...")
    digest = hashlib.sha256()
    with requests.get(download_url, headers=headers, stream=True, timeout=60) as r:
        r.raise_for_status()
        with open(target_path, "wb") as f:
            for chunk in r.iter_content(chunk_size=1024 * 1024):
                if chunk:
                    f.write(chunk)
                    digest.update(chunk)
    expected = checksums.get(a["name"])
    if expected and expected.lower() != digest.hexdigest():
        target_path.unlink(missing_ok=True)
        sys.exit(f"SHA256 mismatch for {fname}: файл удалён")
    size_mb = target_path.stat().st_size / (1024 * 1024)
    print(f"Done: {fname} ({size_mb:.2f} MB){'' if expected else ' [checksum not verified]'}")

print(f"\nAll Windows assets successfully downloaded to {desktop_dir}")
