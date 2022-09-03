import zmq

ctx = zmq.Context()
sock = ctx.socket(zmq.SUB)

sock.connect('tcp://localhost:3442')
print("Connected")
sock.setsockopt(zmq.SUBSCRIBE, b'text')
sock.setsockopt(zmq.SUBSCRIBE, b'raw')
sock.setsockopt(zmq.SUBSCRIBE, b'formatted')
sock.setsockopt(zmq.SUBSCRIBE, b'hwevent') # subscribe to all hwevents

while True:
    [topic, msg] = sock.recv_multipart()
    if topic == b'raw':
        decoded = int.from_bytes(msg, byteorder='little')
        print(f'Raw: 0x{decoded:08X}')
    elif topic == b'formatted':
        print(msg.decode('ascii'),end="")
    elif topic.startswith(b'hwevent'):
        print(f'HWEvent: {topic} Msg: {msg}')
        
