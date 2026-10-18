import requests
import sys
from _gh_auth import github_headers

def main():
    headers = github_headers({'Accept': 'application/vnd.github.v3+json'})

    url = 'https://api.github.com/repos/NanoDev1488/NanoDecompiler/actions/workflows/test-itemsadder.yml/dispatches'
    data = {'ref': 'main'}
    resp = requests.post(url, headers=headers, json=data, timeout=15)
    print(f"Dispatch status: {resp.status_code}")
    if resp.status_code not in (200, 204):
        print(f"Response: {resp.text}")

if __name__ == '__main__':
    main()
