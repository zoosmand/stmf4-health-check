#!/usr/bin/env python3
"""Convert a PEM certificate/key pair to DER for the management API upload."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path


def convert(input_path: Path, output_path: Path, pem_type: str) -> None:
    subprocess.run(
        [
            "openssl",
            pem_type,
            "-in",
            str(input_path),
            "-outform",
            "DER",
            "-out",
            str(output_path),
        ],
        check=True,
    )


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Convert an existing PEM certificate and private key into raw "
            "DER binaries suitable for PUT /api/v1/tls/certificate and "
            "PUT /api/v1/tls/private-key."
        )
    )
    parser.add_argument(
        "--certificate",
        type=Path,
        default=Path("TLS/Private/management_server.crt.pem"),
        help="input PEM certificate path",
    )
    parser.add_argument(
        "--key",
        type=Path,
        default=Path("TLS/Private/management_server.key.pem"),
        help="input PEM private key path",
    )
    parser.add_argument(
        "--certificate-out",
        type=Path,
        default=Path("TLS/Private/management_server.crt.der"),
        help="output DER certificate path",
    )
    parser.add_argument(
        "--key-out",
        type=Path,
        default=Path("TLS/Private/management_server.key.der"),
        help="output DER private key path",
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()

    convert(arguments.certificate, arguments.certificate_out, "x509")
    convert(arguments.key, arguments.key_out, "pkey")
    arguments.key_out.chmod(0o600)

    print(f"Wrote {arguments.certificate_out}")
    print(f"Wrote {arguments.key_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
