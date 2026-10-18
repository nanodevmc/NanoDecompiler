import requests
import sys
from _gh_auth import github_headers

def main():
    headers = github_headers()

    run_id = sys.argv[1] if len(sys.argv) > 1 else '38777129059'
    url = f'https://api.github.com/repos/NanoDev1488/NanoDecompiler/actions/runs/{run_id}/cancel'
    r = requests.post(url, headers=headers, timeout=15)
    print("Cancel status:", r.status_code)

if __name__ == '__main__':
    main()
