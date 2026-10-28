#!/usr/bin/env bash
set -euo pipefail
cd /opt/mwnet/discovery-appliance
sha256sum --check payload.sha256
bash ./setup-host.sh
docker load --input images.tar
# Verify that loading the archive restored the exact image contents, not just tags.
while read -r reference expected; do
    actual=$(docker image inspect --format '{{.Id}}' "$reference")
    [[ $actual == "$expected" ]] || { echo "Image mismatch: $reference" >&2; exit 1; }
done < images.lock
DIRECTORY_HOST=directory.example.invalid docker compose -p mwnet-directory -f compose.yml config --quiet
install -d -m 0755 /var/lib/mwnet-appliance
install -m 0644 /dev/null /var/lib/mwnet-appliance/ready
echo 'MWNet appliance dependencies and images are ready. Public directory remains stopped.'
echo 'Next: sudo tailscale up; then follow /opt/mwnet/discovery-appliance/README.md.'
