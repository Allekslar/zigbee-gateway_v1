#!/usr/bin/env python3
"""Serve a directory over HTTPS with single-range GET/HEAD support."""

from __future__ import annotations

import argparse
import functools
import http.server
import os
import pathlib
import re
import shutil
import ssl
import sys
from http import HTTPStatus


_RANGE_RE = re.compile(r"bytes=(\d*)-(\d*)\Z")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="0.0.0.0", help="Address to bind (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=8443, help="TCP port to listen on (default: 8443)")
    parser.add_argument("--directory", required=True, help="Directory to serve")
    parser.add_argument("--certfile", required=True, help="Server certificate PEM file")
    parser.add_argument("--keyfile", required=True, help="Server private key PEM file")
    return parser.parse_args()


def parse_range_header(value: str | None, size: int) -> tuple[int, int] | None:
    if not value:
        return None
    match = _RANGE_RE.fullmatch(value.strip())
    if not match:
        return None

    start_text, end_text = match.groups()
    if not start_text and not end_text:
        return None

    if not start_text:
        length = int(end_text)
        if length <= 0:
            return None
        if length >= size:
            return (0, size - 1)
        return (size - length, size - 1)

    start = int(start_text)
    if start >= size:
        return None

    if not end_text:
        return (start, size - 1)

    end = int(end_text)
    if end < start:
        return None
    return (start, min(end, size - 1))


class RangeRequestHandler(http.server.SimpleHTTPRequestHandler):
    server_version = "RangeHTTPS/1.0"

    def send_head(self):  # type: ignore[override]
        path = self.translate_path(self.path)
        if os.path.isdir(path):
            return super().send_head()

        ctype = self.guess_type(path)
        try:
            fp = open(path, "rb")
        except OSError:
            self.send_error(HTTPStatus.NOT_FOUND, "File not found")
            return None

        try:
            fs = os.fstat(fp.fileno())
            size = fs.st_size
            range_spec = parse_range_header(self.headers.get("Range"), size)
            if self.headers.get("Range") and range_spec is None:
                self.send_response(HTTPStatus.REQUESTED_RANGE_NOT_SATISFIABLE)
                self.send_header("Content-Range", f"bytes */{size}")
                self.send_header("Accept-Ranges", "bytes")
                self.end_headers()
                fp.close()
                return None

            if range_spec is None:
                start = 0
                end = size - 1
                status = HTTPStatus.OK
            else:
                start, end = range_spec
                status = HTTPStatus.PARTIAL_CONTENT

            length = max(0, end - start + 1)
            self.send_response(status)
            self.send_header("Content-type", ctype)
            self.send_header("Content-Length", str(length))
            self.send_header("Accept-Ranges", "bytes")
            self.send_header("Last-Modified", self.date_time_string(fs.st_mtime))
            if status == HTTPStatus.PARTIAL_CONTENT:
                self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
            self.end_headers()
            self._range = (start, end)
            return fp
        except Exception:
            fp.close()
            raise

    def copyfile(self, source, outputfile):  # type: ignore[override]
        range_spec = getattr(self, "_range", None)
        if range_spec is None:
            shutil.copyfileobj(source, outputfile)
            return

        start, end = range_spec
        remaining = end - start + 1
        source.seek(start)
        while remaining > 0:
            chunk = source.read(min(64 * 1024, remaining))
            if not chunk:
                break
            outputfile.write(chunk)
            remaining -= len(chunk)


def main() -> int:
    args = parse_args()
    directory = pathlib.Path(args.directory).resolve()
    if not directory.is_dir():
        print(f"Directory does not exist: {directory}", file=sys.stderr)
        return 2

    handler = functools.partial(RangeRequestHandler, directory=str(directory))
    httpd = http.server.ThreadingHTTPServer((args.bind, args.port), handler)
    ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ssl_context.load_cert_chain(certfile=args.certfile, keyfile=args.keyfile)
    httpd.socket = ssl_context.wrap_socket(httpd.socket, server_side=True)

    print(f"Serving HTTPS directory {directory} on https://{args.bind}:{args.port}")
    httpd.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
