#!/usr/bin/env python3
"""Serve a directory over HTTPS for local OTA validation."""

from __future__ import annotations

import argparse
import functools
import http.server
import pathlib
import ssl
import sys


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="0.0.0.0", help="Address to bind (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=8443, help="TCP port to listen on (default: 8443)")
    parser.add_argument("--directory", required=True, help="Directory to serve")
    parser.add_argument("--certfile", required=True, help="Server certificate PEM file")
    parser.add_argument("--keyfile", required=True, help="Server private key PEM file")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    directory = pathlib.Path(args.directory).resolve()
    if not directory.is_dir():
        print(f"Directory does not exist: {directory}", file=sys.stderr)
        return 2

    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(directory))
    httpd = http.server.ThreadingHTTPServer((args.bind, args.port), handler)
    ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ssl_context.load_cert_chain(certfile=args.certfile, keyfile=args.keyfile)
    httpd.socket = ssl_context.wrap_socket(httpd.socket, server_side=True)

    print(f"Serving HTTPS directory {directory} on https://{args.bind}:{args.port}")
    httpd.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
