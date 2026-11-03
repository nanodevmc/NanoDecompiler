import os
import re
import json
import urllib.request
import urllib.error

pat = os.environ.get("GH_PAT", "").strip()
if not pat:
    transcript_path = r"C:\Users\nanodev\.gemini\antigravity-cli\brain\44ac420e-0dbe-4c8f-86a9-5effd939bdd3\.system_generated\logs\transcript.jsonl"
    with open(transcript_path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            m = re.search(r'set GH_PAT=([a-zA-Z0-9_]+)', line)
            if m:
                pat = m.group(1).strip()

headers = {
    "Accept": "application/vnd.github+json",
    "User-Agent": "NanoDecompiler-Release-Checker",
    "X-GitHub-Api-Version": "2022-11-28"
}
if pat:
    headers["Authorization"] = f"Bearer {pat}"

url = "https://api.github.com/repos/nanodevmc/NanoDecompiler/releases/tags/v2.0.8"
req = urllib.request.Request(url, headers=headers)

try:
    with urllib.request.urlopen(req) as resp:
        data = json.loads(resp.read().decode("utf-8"))
        print(f"Release: {data.get('name')} (Tag: {data.get('tag_name')})")
        print(f"Created: {data.get('created_at')} | Published: {data.get('published_at')}")
        print(f"HTML URL: {data.get('html_url')}")
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
