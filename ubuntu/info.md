
Snapcraft export your login authentication credentials

    snapcraft export-login credentials.txt
    export SNAPCRAFT_STORE_CREDENTIALS=$(cat credentials.txt)

Retrieve your developer account ID

    joelai@lavender5:~/02_dev/algae-ws/algae-bp/ubuntu$ snapcraft whoami
    email: cupsnow@hotmail.com
    username: cupsnow
    id: YjWC1BAy73VLuSlcgiA60SvvzPpICVdu
    permissions: package_access, package_manage, package_metrics, package_push, package_register, package_release, package_update
    channels: no restrictions
    expires: 2026-04-30T01:12:03.000Z

key

    snapcraft create-key ubuntu-core-24-algae-arm64-key
    Ab-123456

The timestamp in the model assertion must be set to a time and date after the creation of our key

    date -Iseconds --utc


Sign to module

    snap sign -k ubuntu-core-24-algae-arm64-key ubuntu-core-24-algae-arm64.json >ubuntu-core-24-algae-arm64.model

Build image

    ubuntu-image --validation=ignore snap ubuntu-core-24-algae-arm64.model

