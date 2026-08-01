#!/usr/bin/env bash

set -Eeuo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
readonly IFACE="enx00e04f3a434b"
readonly NETWORK="10.121.10.0/24"
readonly ADDRESS="10.121.10.1/24"
readonly CONFIG_FILE="${SCRIPT_DIR}/usb_if.conf"

if (( EUID != 0 )); then
    printf 'Run this script as root (for example: sudo %q).\n' "$0" >&2
    exit 1
fi

for command in ip sysctl iptables dnsmasq; do
    if ! command -v "$command" >/dev/null 2>&1; then
        printf 'Required command not found: %s\n' "$command" >&2
        exit 1
    fi
done

if ! ip link show dev "$IFACE" >/dev/null 2>&1; then
    printf 'Network interface not found: %s\n' "$IFACE" >&2
    exit 1
fi

address_added=0
nat_rule_added=0

cleanup() {
    local status=$?
    trap - EXIT INT TERM

    if (( nat_rule_added )); then
        iptables -t nat -D POSTROUTING -s "$NETWORK" ! -o "$IFACE" \
            -m comment --comment dnsmasq-usb-if -j MASQUERADE || true
    fi
    if (( address_added )); then
        ip address del "$ADDRESS" dev "$IFACE" || true
    fi

    exit "$status"
}
trap cleanup EXIT INT TERM

if ! ip -4 -o address show dev "$IFACE" | awk '{ print $4 }' | grep -Fxq "$ADDRESS"; then
    ip address add "$ADDRESS" dev "$IFACE"
    address_added=1
fi
ip link set dev "$IFACE" up

sysctl -q -w net.ipv4.ip_forward=1

if ! iptables -t nat -C POSTROUTING -s "$NETWORK" ! -o "$IFACE" \
    -m comment --comment dnsmasq-usb-if -j MASQUERADE 2>/dev/null; then
    iptables -t nat -A POSTROUTING -s "$NETWORK" ! -o "$IFACE" \
        -m comment --comment dnsmasq-usb-if -j MASQUERADE
    nat_rule_added=1
fi

cd -- "$SCRIPT_DIR"
dnsmasq --conf-file="$CONFIG_FILE" --no-daemon --log-queries
