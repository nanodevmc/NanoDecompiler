import requests
import sys
from _gh_auth import github_headers

def main():
    headers = github_headers()

    job_id = sys.argv[1] if len(sys.argv) > 1 else '116567578045'
    url = f'https://api.github.com/repos/NanoDev1488/NanoDecompiler/actions/jobs/{job_id}'
    r = requests.get(url, headers=headers, timeout=15)
    data = r.json()
    print("Status:", data.get('status'), "Conclusion:", data.get('conclusion'))
    for s in data.get('steps', []):
        print(f"Step: {s.get('name')} | Status: {s.get('status')} | Conclusion: {s.get('conclusion')}")

if __name__ == '__main__':
    main()
