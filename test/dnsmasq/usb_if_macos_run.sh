#!/usr/bin/env bash

set -Eeuo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
readonly IFACE="en5"
readonly NETWORK="10.121.10.0/24"
readonly ADDRESS="10.121.10.1"
readonly NETMASK="255.255.255.0"
readonly PF_ANCHOR="com.apple/dnsmasq-usb-if"
readonly CONFIG_FILE="${SCRIPT_DIR}/usb_if_macos.conf"

if [[ "$(uname -s)" != "Darwin" ]]; then
    printf 'This launcher is only for macOS.\n' >&2
    exit 1
fi
if (( EUID != 0 )); then
    printf 'Run this script as root (for example: sudo %q).\n' "$0" >&2
    exit 1
fi

for command in ifconfig route sysctl pfctl awk grep; do
    if ! command -v "$command" >/dev/null 2>&1; then
        printf 'Required command not found: %s\n' "$command" >&2
        exit 1
    fi
done

dnsmasq_bin=""
for candidate in /opt/homebrew/sbin/dnsmasq /usr/local/sbin/dnsmasq; do
    if [[ -x "$candidate" ]]; then
        dnsmasq_bin="$candidate"
        break
    fi
done
if [[ -z "$dnsmasq_bin" ]]; then
    printf 'dnsmasq was not found. Install it with: brew install dnsmasq\n' >&2
    exit 1
fi
readonly dnsmasq_bin

if ! ifconfig "$IFACE" >/dev/null 2>&1; then
    printf 'Network interface not found: %s\n' "$IFACE" >&2
    exit 1
fi

readonly WAN_IFACE="$(route -n get default | awk '/interface:/{ print $2; exit }')"
if [[ -z "$WAN_IFACE" ]]; then
    printf 'Could not determine the default outbound interface.\n' >&2
    exit 1
fi

address_added=0
pf_anchor_loaded=0
readonly forwarding_before="$(sysctl -n net.inet.ip.forwarding)"

cleanup() {
    local status=$?
    trap - EXIT INT TERM

    if (( pf_anchor_loaded )); then
        pfctl -a "$PF_ANCHOR" -F all >/dev/null 2>&1 || true
    fi
    if (( address_added )); then
        ifconfig "$IFACE" inet "$ADDRESS" -alias || true
    fi
    if [[ "$forwarding_before" == "0" ]]; then
        sysctl -q -w net.inet.ip.forwarding=0 || true
    fi

    exit "$status"
}
trap cleanup EXIT INT TERM

if ! ifconfig "$IFACE" | awk '$1 == "inet" { print $2 }' | grep -Fxq "$ADDRESS"; then
    ifconfig "$IFACE" inet "$ADDRESS" netmask "$NETMASK" alias
    address_added=1
fi
ifconfig "$IFACE" up

sysctl -q -w net.inet.ip.forwarding=1
printf 'nat on %s from %s to any -> (%s)\n' "$WAN_IFACE" "$NETWORK" "$WAN_IFACE" \
    | pfctl -a "$PF_ANCHOR" -f -
pf_anchor_loaded=1
pfctl -E >/dev/null 2>&1 || true

cd -- "$SCRIPT_DIR"
"$dnsmasq_bin" --conf-file="$CONFIG_FILE" --no-daemon --log-queries
