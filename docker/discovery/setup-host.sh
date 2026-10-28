#!/usr/bin/env bash
# Prepare a fresh Ubuntu Server 24.04/26.04 amd64 host, not a developer workstation.
# Sources: https://docs.docker.com/engine/install/ubuntu/
#          https://pkgs.tailscale.com/stable/#ubuntu-resolute
set -euo pipefail

die() { printf 'Error: %s\n' "$*" >&2; exit 1; }

usage() {
    cat <<'EOF'
Usage: sudo bash setup-host.sh [--dry-run | --help]

Target: Ubuntu Server 24.04 or 26.04 LTS, amd64, with systemd and internet access.
Installs Docker Engine, Buildx, Compose, Tailscale, OpenSSH, Git, curl and CA certificates.
Enables Docker, tailscaled and OpenSSH. Uses official signed apt repositories.
Existing packages may be upgraded. Conflicting container packages are never removed.
Preserves SSH/firewall/Docker daemon settings and does not add docker-group members.
Does not enroll Tailscale, deploy containers, configure DNS/tunnels, or reboot.
--dry-run prints the plan without host checks, downloads or changes; it is not validation.
EOF
}

ubuntu_suite() {
    # Derive repository names from an explicit allowlist, never environment codenames.
    case "${1:-}" in
        24.04) printf '%s\n' noble ;;
        26.04) printf '%s\n' resolute ;;
        *) die "Supported Ubuntu versions are 24.04 and 26.04; found ${1:-unset}." ;;
    esac
}

validate_platform() {
    [[ $1 == ubuntu && $3 == amd64 ]] ||
        die "Supported targets are Ubuntu 24.04/26.04 amd64; found $1 $2 $3."
    ubuntu_suite "$2" >/dev/null
}

check_conflicts() {
    local package status
    local -a conflicts=()
    for package in docker.io docker-compose docker-compose-v2 docker-doc docker-buildx podman-docker containerd runc; do
        status=$(dpkg-query -W -f='${Status}' "$package" 2>/dev/null || true)
        if [[ $status == 'install ok installed' ]]; then
            conflicts+=("$package")
        fi
    done
    ((${#conflicts[@]} == 0)) ||
        die "Conflicting packages: ${conflicts[*]}. Review existing workloads and migrate manually first."
}

docker_source() {
    local suite
    suite=$(ubuntu_suite "${1:-}") || return
    cat <<EOF
Types: deb
URIs: https://download.docker.com/linux/ubuntu
Suites: $suite
Components: stable
Architectures: amd64
Signed-By: /etc/apt/keyrings/docker.asc
EOF
}

tailscale_source() {
    local suite
    suite=$(ubuntu_suite "${1:-}") || return
    printf 'deb [signed-by=/usr/share/keyrings/tailscale-archive-keyring.gpg] https://pkgs.tailscale.com/stable/ubuntu %s main\n' "$suite"
}

tailscale_key_url() {
    local suite
    suite=$(ubuntu_suite "${1:-}") || return
    printf 'https://pkgs.tailscale.com/stable/ubuntu/%s.noarmor.gpg\n' "$suite"
}

# Refuse to replace customized repository files, including symlinks.
check_source() {
    local path=$1 expected=$2
    [[ ! -L $path ]] || die "Repository file is a symlink: $path"
    if [[ -e $path ]]; then
        [[ -f $path && $(cat "$path") == "$expected" ]] ||
            die "Existing repository differs: $path. Reconcile it manually before retrying."
    fi
}

preflight() {
    ((EUID == 0)) || die 'Run with sudo on the intended server.'
    local ID='' VERSION_ID=''
    # shellcheck disable=SC1091
    source /etc/os-release
    validate_platform "${ID:-}" "${VERSION_ID:-}" "$(dpkg --print-architecture)"
    [[ -d /run/systemd/system ]] || die 'A booted systemd host is required.'
    check_conflicts
    check_source /etc/apt/sources.list.d/docker.sources "$(docker_source "$VERSION_ID")"
    check_source /etc/apt/sources.list.d/tailscale.list "$(tailscale_source "$VERSION_ID")"
    local path
    for path in /etc/apt/sources.list /etc/apt/sources.list.d/*.list /etc/apt/sources.list.d/*.sources; do
        [[ -f $path ]] || continue
        case "$path" in
            /etc/apt/sources.list.d/docker.sources|/etc/apt/sources.list.d/tailscale.list) continue ;;
        esac
        if grep -Eq 'download\.docker\.com|pkgs\.tailscale\.com' "$path"; then
            die "Another Docker/Tailscale repository exists in $path. Reconcile it manually first."
        fi
    done
    printf '%s\n' "$VERSION_ID"
}

verify_runtime() {
    local service
    /usr/sbin/sshd -t
    systemctl enable --now docker.service tailscaled.service ssh.service
    for service in docker.service tailscaled.service ssh.service; do
        systemctl is-active --quiet "$service"
    done
    docker info >/dev/null
    docker compose version
    docker buildx version
    tailscale version
}

install_requirements() (
    local version=${1:-} key_url
    key_url=$(tailscale_key_url "$version")
    # Keep temporary downloads separate and remove only this invocation's directory.
    local scratch
    scratch=$(mktemp -d)
    trap 'rm -rf -- "$scratch"' EXIT
    export DEBIAN_FRONTEND=noninteractive
    local -a apt=(apt-get -o DPkg::Lock::Timeout=120 -o Dpkg::Options::=--force-confold -o APT::Update::Error-Mode=any)
    "${apt[@]}" update
    "${apt[@]}" install -y --no-remove ca-certificates curl git openssh-server
    curl --fail --silent --show-error --location --proto '=https' --proto-redir '=https' \
        --connect-timeout 10 --max-time 120 --retry 3 \
        https://download.docker.com/linux/ubuntu/gpg -o "$scratch/docker.asc"
    curl --fail --silent --show-error --location --proto '=https' --proto-redir '=https' \
        --connect-timeout 10 --max-time 120 --retry 3 \
        "$key_url" -o "$scratch/tailscale.gpg"
    [[ -s $scratch/docker.asc && -s $scratch/tailscale.gpg ]] || die 'Empty repository signing key.'
    install -d -m 0755 /etc/apt/keyrings /usr/share/keyrings /etc/apt/sources.list.d
    install -m 0644 "$scratch/docker.asc" /etc/apt/keyrings/docker.asc
    install -m 0644 "$scratch/tailscale.gpg" /usr/share/keyrings/tailscale-archive-keyring.gpg
    docker_source "$version" > "$scratch/docker.sources"
    tailscale_source "$version" > "$scratch/tailscale.list"
    install -m 0644 "$scratch/docker.sources" /etc/apt/sources.list.d/docker.sources
    install -m 0644 "$scratch/tailscale.list" /etc/apt/sources.list.d/tailscale.list
    "${apt[@]}" update
    "${apt[@]}" install -y --no-remove docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin tailscale
    verify_runtime
)

main() {
    case "${1:-}" in
        --help|-h) (($# == 1)) || die 'Unexpected extra arguments.'; usage; return ;;
        --dry-run)
            (($# == 1)) || die 'Unexpected extra arguments.'
            usage
            printf '\nPlan: validate host and repository conflicts; install prerequisites; add signed\nDocker/Tailscale repositories; install packages; enable services; check runtime.\n'
            return ;;
        '') (($# == 0)) || die 'Unexpected empty argument.' ;;
        *) die "Unknown option: $1 (use --help)." ;;
    esac
    local version
    # Only the validated /etc/os-release version reaches package installation.
    version=$(preflight)
    install_requirements "$version"
    cat <<'EOF'

Host dependencies are ready. No directory has been deployed.
Next, from the server console or your existing SSH session:
  sudo tailscale up
  tailscale ip -4
Follow the authentication URL, then connect from a computer on the same tailnet:
  ssh YOUR_LINUX_USER@SERVER_TAILSCALE_IP
Use your existing Linux user and SSH key; Tailscale enrollment does not install a key.
Restrict tailnet access to your administrators. Test a second SSH session before
changing authentication or firewall rules. See docs/public-discovery.md for deployment.
EOF
    if [[ -e /var/run/reboot-required ]]; then
        printf '\nThe OS reports a reboot is required; schedule it after checking access.\n'
    fi
}

if [[ ${BASH_SOURCE[0]} == "$0" ]]; then
    main "$@"
fi
