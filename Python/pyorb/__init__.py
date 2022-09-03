from ctypes import *
import socket

orb = CDLL("liborb.so.1")

# Ensure restype is the correct length for platform
class my_void_p(c_void_p): 
    pass

class TSMsg(Structure):
    _fields_ = [ ("ts",         c_longlong),
                 ("timeStatus", c_int,8),
                 ("timeInc",    c_int)]

class swMsg(Structure):
    _fields_ = [ ("ts",         c_longlong),
                 ("srcAddr",    c_int,8),
                 ("len",        c_int,8),
                 ("value",      c_int)]

class nisyncMsg(Structure):
    _fields_ = [ ("ts",         c_longlong),
                 ("type",       c_int,8),
                 ("addr",       c_int)]

class pcSampleMsg(Structure):
    _fields_ = [ ("ts",         c_longlong),
                 ("sleep",      c_bool,1),
                 ("pc",         c_int)]


class oswMsg(Structure):
    _fields_ = [ ("ts",         c_longlong),
                 ("comp",       c_int,8),
                 ("offset",     c_int)]

class wptMsg(Structure):
    _fields_ = [ ("ts",         c_longlong),
                 ("comp",       c_int,8),
                 ("data",       c_int)]

class watchMsg(Structure):
    _fields_ = [ ("ts",         c_longlong),
                 ("comp",       c_int,8),
                 ("isWrite",    c_bool,1),
                 ("data",       c_int)]

class dwtMsg(Structure):
    _fields_ = [ ("ts",         c_longlong),
                 ("event",      c_int,8)]

class Empty(Structure):
    _fields_ = [ ("ts",         c_longlong)]


class excMsg(Structure):
    _fields_ = [ ("ts",         c_longlong),
                 ("exceptionNumber",c_int),
                 ("eventType",      c_int,8)]

class msgUnion(Union):
    _fields_ = [ ("Unknown",     Empty),
                 ("Reserved",    Empty),
                 ("Error",       Empty),
                 ("None",        Empty),
                 ("swMsg",       swMsg),
                 ("nisyncMsg",   nisyncMsg),
                 ("oswMsg",      oswMsg),
                 ("watchMsg",    watchMsg),
                 ("wptMsg",      wptMsg),
                 ("pcSampleMsg", pcSampleMsg),
                 ("dwtMsg",      dwtMsg),
                 ("excMsg",      excMsg),
                 ("TSMsg",       TSMsg)]

class msg(Structure):
    _fields_ = [ ("msgtype",     c_int),
                 ("m",           msgUnion) ]
    
orb.ITMDecoderCreate.restype = my_void_p
orb.ITMDecoderInit.argtypes = [ my_void_p, c_bool ]
orb.ITMPump.argtypes = [ my_void_p, c_char ]
orb.ITMGetDecodedPacket.argtypes = [my_void_p, my_void_p]
orb.ITMGetDecodedPacket.restype = c_bool

orb.ITM_EV_NONE       = 0
orb.ITM_EV_PACKET_RXED= 1
orb.ITM_EV_UNSYNCED   = 2
orb.ITM_EV_SYNCED     = 3
orb.ITM_EV_OVERFLOW   = 4
orb.ITM_EV_ERROR      = 5

class Orb:
    def __init__(self, addr=("localhost",3443), withTPIU=False, forceSync=True, sock=None):
        self.addr = addr

        if sock is None:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        else:
            self.sock = sock

        self.sock.connect(self.addr)
        self.itm = orb.ITMDecoderCreate()
        orb.ITMDecoderInit( self.itm, forceSync )

    def rx(self):
        while (True):
            c = self.sock.recv(1)
            if (orb.ITM_EV_PACKET_RXED==orb.ITMPump( self.itm, c[0] )):
                p = msg()
                orb.ITMGetDecodedPacket( self.itm, byref(p))
                try:
                    return(getattr(p.m,p.m._fields_[p.msgtype][0]))
                except IndexError:
                    return Empty

