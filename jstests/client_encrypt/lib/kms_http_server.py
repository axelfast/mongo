#! /usr/bin/env python3
"""Mock AWS KMS Endpoint."""

import argparse
import collections
import base64
import http.server
import json
import logging
import socketserver
import sys
import urllib.parse
import ssl

import kms_http_common

SECRET_PREFIX = "00SECRET"

# Pass this data out of band instead of storing it in AwsKmsHandler since the
# BaseHTTPRequestHandler does not call the methods as object methods but as class methods. This
# means there is not self.
stats = kms_http_common.Stats()
disable_faults = False
fault_type = None

"""Fault which causes encrypt to return 500."""
FAULT_ENCRYPT = "fault_encrypt"

"""Fault which causes encrypt to return an error that contains a type and message"""
FAULT_ENCRYPT_CORRECT_FORMAT = "fault_encrypt_correct_format"

"""Fault which causes encrypt to return wrong fields in JSON."""
FAULT_ENCRYPT_WRONG_FIELDS = "fault_encrypt_wrong_fields"

"""Fault which causes encrypt to return bad BASE64."""
FAULT_ENCRYPT_BAD_BASE64 = "fault_encrypt_bad_base64"

"""Fault which causes decrypt to return 500."""
FAULT_DECRYPT = "fault_decrypt"

"""Fault which causes decrypt to return an error that contains a type and message"""
FAULT_DECRYPT_CORRECT_FORMAT = "fault_decrypt_correct_format"

"""Fault which causes decrypt to return wrong key."""
FAULT_DECRYPT_WRONG_KEY = "fault_decrypt_wrong_key"


# List of supported fault types
SUPPORTED_FAULT_TYPES = [
    FAULT_ENCRYPT,
    FAULT_ENCRYPT_CORRECT_FORMAT,
    FAULT_ENCRYPT_WRONG_FIELDS,
    FAULT_ENCRYPT_BAD_BASE64,
    FAULT_DECRYPT,
    FAULT_DECRYPT_CORRECT_FORMAT,
    FAULT_DECRYPT_WRONG_KEY,
]

class AwsKmsHandler(http.server.BaseHTTPRequestHandler):
    """
    Handle requests from AWS KMS Monitoring and test commands
    """
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        """Serve a Test GET request."""
        parts = urllib.parse.urlsplit(self.path)
        path = parts[2]

        if path == kms_http_common.URL_PATH_STATS:
            self._do_stats()
        elif path == kms_http_common.URL_DISABLE_FAULTS:
            self._do_disable_faults()
        elif path == kms_http_common.URL_ENABLE_FAULTS:
            self._do_enable_faults()
        else:
            self.send_response(http.HTTPStatus.NOT_FOUND)
            self.end_headers()
            self.wfile.write("Unknown URL".encode())

    def do_POST(self):
        """Serve a POST request."""
        parts = urllib.parse.urlsplit(self.path)
        path = parts[2]

        if path == "/":
            self._do_post()
        else:
            self.send_response(http.HTTPStatus.NOT_FOUND)
            self.end_headers()
            self.wfile.write("Unknown URL".encode())

    def _send_reply(self, data, status=http.HTTPStatus.OK):
        print("Sending Response: " + data.decode())

        self.send_response(status)
        self.send_header("content-type", "application/octet-stream")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()

        self.wfile.write(data)

    def _do_post(self):
        global stats
        clen = int(self.headers.get('content-length'))

        raw_input = self.rfile.read(clen)

        print("RAW INPUT: " + str(raw_input))

        # X-Amz-Target: TrentService.Encrypt
        aws_operation = self.headers['X-Amz-Target']

        if aws_operation == "TrentService.Encrypt":
            stats.encrypt_calls += 1
            self._do_encrypt(raw_input)
        elif aws_operation == "TrentService.Decrypt":
            stats.decrypt_calls += 1
            self._do_decrypt(raw_input)
        else:
            data = "Unknown AWS Operation"
            self._send_reply(data.encode("utf-8"))

    def _do_encrypt(self, raw_input):
        request = json.loads(raw_input)

        print(request)

        plaintext = request["Plaintext"]
        keyid = request["KeyId"]

        ciphertext = SECRET_PREFIX.encode() + plaintext.encode()
        ciphertext = base64.b64encode(ciphertext).decode()

        if fault_type and fault_type.startswith(FAULT_ENCRYPT) and not disable_faults:
            return self._do_encrypt_faults(ciphertext)

        response = {
            "CiphertextBlob" : ciphertext,
            "KeyId" : keyid,
        }

        self._send_reply(json.dumps(response).encode('utf-8'))

    def _do_encrypt_faults(self, raw_ciphertext):
        stats.fault_calls += 1

        if fault_type == FAULT_ENCRYPT:
            self._send_reply("Internal Error of some sort.".encode(), http.HTTPStatus.INTERNAL_SERVER_ERROR)
            return
        elif fault_type == FAULT_ENCRYPT_WRONG_FIELDS:
            response = {
                "SomeBlob" : raw_ciphertext,
                "KeyId" : "foo",
            }

            self._send_reply(json.dumps(response).encode('utf-8'))
            return
        elif fault_type == FAULT_ENCRYPT_BAD_BASE64:
            response = {
                "CiphertextBlob" : "foo",
                "KeyId" : "foo",
            }

            self._send_reply(json.dumps(response).encode('utf-8'))
            return
        elif fault_type == FAULT_ENCRYPT_CORRECT_FORMAT:
            response = {
                "__type" : "NotFoundException",
                "message" : "Error encrypting message",
            }

            self._send_reply(json.dumps(response).encode('utf-8'))
            return

        raise ValueError("Unknown Fault Type: " + fault_type)

    def _do_decrypt(self, raw_input):
        request = json.loads(raw_input)
        blob = base64.b64decode(request["CiphertextBlob"]).decode()

        print("FOUND SECRET: " + blob)

        # our "encrypted" values start with the word SECRET_PREFIX otherwise they did not come from us
        if not blob.startswith(SECRET_PREFIX):
            raise ValueError()

        blob = blob[len(SECRET_PREFIX):]

        if fault_type and fault_type.startswith(FAULT_DECRYPT) and not disable_faults:
            return self._do_decrypt_faults(blob)

        response = {
            "Plaintext" : blob,
            "KeyId" : "Not a clue",
        }

        self._send_reply(json.dumps(response).encode('utf-8'))

    def _do_decrypt_faults(self, blob):
        stats.fault_calls += 1

        if fault_type == FAULT_DECRYPT:
            self._send_reply("Internal Error of some sort.".encode(), http.HTTPStatus.INTERNAL_SERVER_ERROR)
            return
        elif fault_type == FAULT_DECRYPT_WRONG_KEY:
            response = {
                "Plaintext" : "ta7DXE7J0OiCRw03dYMJSeb8nVF5qxTmZ9zWmjuX4zW/SOorSCaY8VMTWG+cRInMx/rr/+QeVw2WjU2IpOSvMg==",
                "KeyId" : "Not a clue",
            }

            self._send_reply(json.dumps(response).encode('utf-8'))
            return
        elif fault_type == FAULT_DECRYPT_CORRECT_FORMAT:
            response = {
                "__type" : "NotFoundException",
                "message" : "Error decrypting message",
            }

            self._send_reply(json.dumps(response).encode('utf-8'))
            return

        raise ValueError("Unknown Fault Type: " + fault_type)

    def _send_header(self):
        self.send_response(http.HTTPStatus.OK)
        self.send_header("content-type", "application/octet-stream")
        self.end_headers()

    def _do_stats(self):
        self._send_header()

        self.wfile.write(str(stats).encode('utf-8'))

    def _do_disable_faults(self):
        global disable_faults
        disable_faults = True
        self._send_header()

    def _do_enable_faults(self):
        global disable_faults
        disable_faults = False
        self._send_header()

def run(port, cert_file, ca_file, server_class=http.server.HTTPServer, handler_class=AwsKmsHandler):
    """Run web server."""
    server_address = ('', port)

    httpd = server_class(server_address, handler_class)

    httpd.socket = ssl.wrap_socket (httpd.socket,
        certfile=cert_file,
        ca_certs=ca_file, server_side=True)

    print("Mock KMS Web Server Listening on %s" % (str(server_address)))

    httpd.serve_forever()


def main():
    """Main Method."""
    global fault_type
    global disable_faults

    parser = argparse.ArgumentParser(description='MongerDB Mock AWS KMS Endpoint.')

    parser.add_argument('-p', '--port', type=int, default=8000, help="Port to listen on")

    parser.add_argument('-v', '--verbose', action='count', help="Enable verbose tracing")

    parser.add_argument('--fault', type=str, help="Type of fault to inject")

    parser.add_argument('--disable-faults', action='store_true', help="Disable faults on startup")

    parser.add_argument('--ca_file', type=str, required=True, help="TLS CA PEM file")

    parser.add_argument('--cert_file', type=str, required=True, help="TLS Server PEM file")

    args = parser.parse_args()
    if args.verbose:
        logging.basicConfig(level=logging.DEBUG)

    if args.fault:
        if args.fault not in SUPPORTED_FAULT_TYPES:
            print("Unsupported fault type %s, supports types are %s" % (args.fault, SUPPORTED_FAULT_TYPES))
            sys.exit(1)

        fault_type = args.fault

    if args.disable_faults:
        disable_faults = True

    run(args.port, args.cert_file, args.ca_file)


if __name__ == '__main__':

    main()
