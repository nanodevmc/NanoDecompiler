import os
import json
import urllib.request
import urllib.error

pat = os.environ.get("GH_PAT", "").strip()
headers = {
    "Accept": "application/vnd.github+json",
    "User-Agent": "NanoDecompiler-Release-Checker"
}
if pat:
    headers["Authorization"] = f"Bearer {pat}"

url = "https://api.github.com/repos/nanodevmc/NanoDecompiler/releases/tags/v2.0.7"
req = urllib.request.Request(url, headers=headers)

try:
    with urllib.request.urlopen(req) as resp:
        data = json.loads(resp.read().decode("utf-8"))
        print(f"Release: {data.get('name')} (Tag: {data.get('tag_name')})")
        print(f"Created: {data.get('created_at')} | Published: {data.get('published_at')}")
        assets = data.get("assets", [])
        print(f"Total assets: {len(assets)}")
        print("--------------------------------------------------------------------------------")
        for a in assets:
            size_mb = a['size'] / (1024 * 1024)
            size_kb = a['size'] / 1024
            size_str = f"{size_mb:.2f} MB" if size_mb >= 1.0 else f"{size_kb:.1f} KB"
            print(f"- {a['name']:<55} | {size_str:>9} | ID: {a['id']}")
except urllib.error.HTTPError as e:
    print(f"HTTP Error {e.code}: {e.read().decode('utf-8')}")
except Exception as e:
    print(f"Error: {e}")
