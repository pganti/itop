# listener for multicast stats
import socket
import struct
import json
import os
import errno
from datetime import datetime

MCAST_GRP = '224.0.0.251'
MCAST_PORT = 6000
DIR="/mnt/filer/HealthInfo/"
SUFFIX=".json"
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

# bind this to the master node's IP address
#sock.setsockopt(socket.SOL_IP,socket.IP_ADD_MEMBERSHIP,
#                socket.inet_aton(MCAST_GRP)+socket.inet_aton('10.100.6.43'))

sock.bind((MCAST_GRP, MCAST_PORT))
mreq = struct.pack("4sl", socket.inet_aton(MCAST_GRP), socket.INADDR_ANY)

sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
while True:
    data, srv_sock = sock.recvfrom(8192)
    srv_addr, srv_srcport = srv_sock[0], srv_sock[1]
    mobj = json.loads(data.replace('\0',''))
    mobj.update({'ip': srv_addr})
    filename=DIR+str(datetime.now().month)+"_"+str(datetime.now().day)+"_"+str(datetime.now().hour)+"_"+mobj["cluster"]+"_"+srv_addr+SUFFIX
    try:
        os.makedirs(os.path.dirname(filename))
    except OSError as exc: # Guard against race condition
        if exc.errno != errno.EEXIST:
            raise
    with open(filename, 'a') as fout:
        fout.write(json.dumps(mobj)+"\n")
