#!/usr/bin/env python3
"""F05 loopback-only RTSP/H264 server, RTP over TCP. Synthetic frames only."""
import argparse
import base64
import re
import socket
import struct
import threading
import time
from pathlib import Path
from urllib.parse import urlsplit

SAMPLE=Path(__file__).resolve().parent.parent/"assets/f05_test_320x240.h264"
NAL_UNITS=[x for x in re.split(b"\x00\x00\x01|\x00\x00\x00\x01",
                              SAMPLE.read_bytes()) if x]
SPS=next(x for x in NAL_UNITS if x[0]&31==7)
PPS=next(x for x in NAL_UNITS if x[0]&31==8)
FRAMES=[]
frame=[]
for nal in NAL_UNITS:
    if (nal[0]&31)==9:
        if frame: FRAMES.append(frame)
        frame=[]
    else: frame.append(nal)
if frame: FRAMES.append(frame)


class RtspClient:
    def __init__(self,sock):
        self.sock=sock
        self.lock=threading.Lock()
        self.running=True
        self.playing=False
        self.seq=1000
        self.timestamp=0
        self.channel=0
        self.ssrc=0xF0512026

    def write(self,data):
        if not self.running:return
        with self.lock:
            try:self.sock.sendall(data)
            except OSError:self.running=False

    def answer(self,cseq,code=200,headers=None,body=b""):
        phrases={200:"OK",404:"Not Found",461:"Unsupported Transport",
                 501:"Not Implemented"}
        lines=[f"RTSP/1.0 {code} {phrases.get(code,'Error')}",
               f"CSeq: {cseq}","Server: GXWB-F05-Loopback"]
        for key,value in (headers or {}).items():
            lines.append(f"{key}: {value}")
        lines.append(f"Content-Length: {len(body)}")
        lines.append("")
        lines.append("")
        self.write("\r\n".join(lines).encode()+body)

    def rtp(self,payload,marker):
        header=struct.pack("!BBHII",0x80,0x60|(0x80 if marker else 0),
                           self.seq & 0xffff,self.timestamp,self.ssrc)
        self.seq+=1
        packet=header+payload
        self.write(b"$"+bytes([self.channel])+struct.pack("!H",len(packet))+packet)

    def nal(self,content,last):
        if len(content)<=1200:
            self.rtp(content,last)
            return
        first=content[0]
        indicator=(first&0xe0)|28
        nal_type=first&31
        data=content[1:]
        for i in range(0,len(data),1100):
            chunk=data[i:i+1100]
            beginning=i==0
            ending=i+len(chunk)>=len(data)
            fu_header=nal_type|(0x80 if beginning else 0)|(0x40 if ending else 0)
            self.rtp(bytes([indicator,fu_header])+chunk,last and ending)

    def stream(self):
        idx=0
        while self.running and self.playing:
            frame=FRAMES[idx % len(FRAMES)]
            nals=[x for x in frame if (x[0]&31) not in (9,6)]
            for j,nal in enumerate(nals):
                self.nal(nal,j==len(nals)-1)
            idx+=1
            self.timestamp=(self.timestamp+9000)&0xffffffff
            time.sleep(0.1)

    def serve(self):
        self.sock.settimeout(8)
        file=self.sock.makefile("rb")
        try:
            while self.running:
                line=file.readline()
                if not line:break
                if line.startswith(b"$"):
                    # Interleaved RTCP reports from the client.
                    length=struct.unpack("!H",file.read(2))[0]
                    file.read(length)
                    continue
                parts=line.decode("latin1","replace").strip().split(" ")
                if len(parts)<3:continue
                method,uri,_=parts[:3]
                headers={}
                while True:
                    line=file.readline()
                    if line in (b"\r\n",b"\n",b""):break
                    if b":" in line:
                        k,v=line.split(b":",1)
                        headers[k.decode().lower()]=v.decode("latin1").strip()
                length=int(headers.get("content-length","0"))
                if length:file.read(length)
                cseq=headers.get("cseq","1")
                if method=="OPTIONS":
                    self.answer(cseq,headers={"Public":"OPTIONS, DESCRIBE, SETUP, PLAY, GET_PARAMETER, TEARDOWN"})
                elif method=="DESCRIBE":
                    sprop=base64.b64encode(SPS).decode()+","+base64.b64encode(PPS).decode()
                    profile=SPS[1:4].hex()
                    body=(
                        "v=0\r\n"
                        "o=- 0 0 IN IP4 127.0.0.1\r\n"
                        "s=GXWB-F05-Test\r\n"
                        "c=IN IP4 127.0.0.1\r\n"
                        "t=0 0\r\n"
                        "a=control:*\r\n"
                        "m=video 0 RTP/AVP 96\r\n"
                        "a=rtpmap:96 H264/90000\r\n"
                        f"a=fmtp:96 packetization-mode=1;profile-level-id={profile};sprop-parameter-sets={sprop}\r\n"
                        "a=control:trackID=0\r\n"
                    ).encode()
                    self.answer(cseq,headers={"Content-Type":"application/sdp",
                                              "Content-Base":uri.split("trackID")[0]},body=body)
                elif method=="SETUP":
                    transport=headers.get("transport","")
                    if "RTP/AVP/TCP" not in transport.upper():
                        self.answer(cseq,461)
                        continue
                    found=re.search(r"interleaved=(\d+)-(\d+)",transport)
                    self.channel=int(found.group(1)) if found else 0
                    self.answer(cseq,headers={
                        "Transport":f"RTP/AVP/TCP;unicast;interleaved={self.channel}-{self.channel+1};ssrc={self.ssrc:08X}",
                        "Session":"F05TEST1234;timeout=60"
                    })
                elif method=="PLAY":
                    self.answer(cseq,headers={
                        "Session":"F05TEST1234",
                        "Range":"npt=0.000-",
                        "RTP-Info":f"url={uri.rstrip('/')}/trackID=0;seq={self.seq};rtptime={self.timestamp}"
                    })
                    if not self.playing:
                        self.playing=True
                        threading.Thread(target=self.stream,daemon=True).start()
                elif method=="GET_PARAMETER":
                    self.answer(cseq,headers={"Session":"F05TEST1234"})
                elif method=="TEARDOWN":
                    self.answer(cseq,headers={"Session":"F05TEST1234"})
                    break
                else:
                    self.answer(cseq,501)
        except (OSError,ValueError) as exc:
            print("RTSP local client finished:",type(exc).__name__,flush=True)
        finally:
            self.running=False
            self.playing=False
            try:file.close()
            except Exception:pass
            self.sock.close()


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--port",type=int,default=0)
    ap.add_argument("--port-file",required=True)
    args=ap.parse_args()
    server=socket.socket()
    server.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
    server.bind(("127.0.0.1",args.port))
    server.listen(5)
    Path(args.port_file).write_text(str(server.getsockname()[1]))
    print("RTSP LOOPBACK PORT",server.getsockname()[1],flush=True)
    while True:
        client,_=server.accept()
        threading.Thread(target=RtspClient(client).serve,daemon=True).start()


if __name__=="__main__":
    main()
