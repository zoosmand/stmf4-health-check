#!/usr/bin/env python3
"""Convert local PEM server credentials into an ignored C header."""

from pathlib import Path


def array(name: str, value: bytes) -> str:
    encoded = ", ".join(f"0x{byte:02X}U" for byte in value + b"\0")
    return (
        f"static const unsigned char {name}[] = {{\n"
        f"  {encoded}\n"
        "};\n"
    )


private_directory = Path("TLS/Private")
certificate = (private_directory / "management_server.crt.pem").read_bytes()
private_key = (private_directory / "management_server.key.pem").read_bytes()
header = private_directory / "management_server_credentials.h"
header.write_text(
    """\
#ifndef MANAGEMENT_SERVER_CREDENTIALS_H
#define MANAGEMENT_SERVER_CREDENTIALS_H

"""
    + array("managementServerCertificate", certificate)
    + "\n"
    + array("managementServerPrivateKey", private_key)
    + "\n#endif /* MANAGEMENT_SERVER_CREDENTIALS_H */\n",
    encoding="utf-8",
)
header.chmod(0o600)
