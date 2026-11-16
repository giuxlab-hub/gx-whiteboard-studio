#!/usr/bin/env python3
"""Loopback-only synthetic HTTP MJPEG camera for F05 QA. No real camera data."""
import argparse
import io
import math
import signal
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from PIL import Image, ImageDraw, ImageFont


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        if self.path.startswith("/health"):
            body=b"F05 LOOPBACK MJPEG TEST\n"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if self.path not in ("/stream", "/drop"):
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", "multipart/x-mixed-replace; boundary=frame")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()
        frame_count=0
        while True:
            frame_count+=1
            image=Image.new("RGB", (640,480), (19,35,57))
            draw=ImageDraw.Draw(image)
            for x in range(0,640,64):
                draw.rectangle((x,0,x+31,130),fill=((x*3)%255,(x*7)%255,(x*11)%255))
            xx=int(320+210*math.sin(frame_count/7))
            draw.ellipse((xx-46,190,xx+46,282),fill=(240,125,62))
            draw.text((28,340),f"GX WHITEBOARD STUDIO - F05 LOCAL STREAM",fill=(255,255,255))
            draw.text((28,385),f"FRAME {frame_count} / {self.path}",fill=(120,220,190))
            content=io.BytesIO()
            image.save(content,format="JPEG",quality=72)
            jpeg=content.getvalue()
            try:
                self.wfile.write(b"--frame\r\n")
                self.wfile.write(b"Content-Type: image/jpeg\r\n")
                self.wfile.write(f"Content-Length: {len(jpeg)}\r\n\r\n".encode("ascii"))
                self.wfile.write(jpeg+b"\r\n")
                self.wfile.flush()
            except (BrokenPipeError,ConnectionResetError,OSError):
                break
            if self.path=="/drop" and frame_count>=20:
                # Intentionally close network stream: F05 should reconnect.
                break
            time.sleep(1/12)

    def log_message(self, fmt, *args):
        # Only loopback client and path; no credentials or personal data.
        return


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--port",type=int,default=0)
    parser.add_argument("--port-file",required=True)
    args=parser.parse_args()
    server=ThreadingHTTPServer(("127.0.0.1",args.port),Handler)
    with open(args.port_file,"w",encoding="ascii") as f:
        f.write(str(server.server_address[1]))
    signal.signal(signal.SIGTERM,lambda *_:server.shutdown())
    print(f"F05 LOOPBACK SERVER PORT {server.server_address[1]}",flush=True)
    server.serve_forever(poll_interval=0.3)


if __name__=="__main__":
    main()
