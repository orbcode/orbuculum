import usb.backend.libusb1
import cmsis_dap

VENDOR_ID = 0x1209
PRODUCT_ID = 0x3443

def write_to_usb(dev, msg_str):

    print(">>>",end="")
    for p in msg_str:
        print(f' 0x{p:02x}', end="")

    try:
	# 0x01 is the OUT endpoint
        num_bytes_written = dev.write(0x02, msg_str)

    except usb.core.USBError as e:
        print (e.args)

    print(f" [{num_bytes_written}]")
    return num_bytes_written

def read_from_usb(dev, timeout):
    try:
	# try to read a maximum of 64 bytes from 0x81 (IN endpoint)
        data = dev.read(0x81, 64, timeout)
    except usb.core.USBError as e:
        print ("Error reading response: {}".format(e.args))
        exit(-1)

    if len(data) == 0:
        print ("Zero length response")
        exit(-1)

    return data

def op_response( d, compareStr ):
    duff=False
    print(" <<<"+str(len(d))+f" bytes [",end="")
    for p,x in zip(d,compareStr):
        if p==x:
            print(f' 0x{p:02x}', end="")
        else:
            duff=True
            print(f' !!0x{p:02x}/0x{x:02x}!!', end="")
    if (duff):
        print(" ] *********************** FAULT **********************")
    else:
        print(" ]")


tests = (
    ( "Short Request",              b"\x19\x19",                    b"\xff"                     ),
    ( "Vendor ID",                  b"\x00\x01",                    b"\x00\x00"                 ),
    ( "Product ID",                 b"\x00\x02",                    b"\x00\x00"                 ),
    ( "Serial Number",              b"\x00\x03",                    b"\x00\x00"                 ),
    ( "Target Device Vendor",       b"\x00\x05",                    b"\x00\x00"                 ),
    ( "Target Device Name",         b"\x00\x06",                    b"\x00\x00"                 ),
    ( "FW version",                 b"\x00\x04",                    b"\x00\x04\x31\x2e\x30\x30" ),
    ( "Illegal command",            b"\x42",                        b"\xff"                     ),
    ( "Request CAPABILITIES",       b"\x00\xf0",                    b"\x00\x01\x01"             ),
    ( "Request TEST DOMAIN TIMER",  b"\x00\xf1",                    b"\x00\x08\x00\xca\x9a\x3b" ),
    ( "Request SWO Trace Buffer Size", b"\x00\xfd",                 b"\x00\x04\xe8\x03\x00\x00" ),
    ( "Request Packet Count",       b"\x00\xFE",                    b"\x00\x01\x40"             ),
    ( "Request Packet Size",        b"\x00\xff",                    b"\x00\x02\x40\x00"         ),
    ( "Set connect led",            b"\x01\x00\x01",                b"\x01\x00"                 ),
    ( "Set running led",            b"\x01\x01\x01",                b"\x01\x00"                 ),
    ( "Set illegal led",            b"\x01\x02\x01",                b"\xff"                     ),
    ( "Connect swd",                b"\x02\x01",                    b"\x02\x01"                 ),
    ( "Connect default",            b"\x02\x00",                    b"\x02\x01"                 ),
    ( "Connect JTAG",               b"\x02\x02",                    b"\xff"                     ),
    ( "Disconnect",                 b"\x03",                        b"\x03\x00"                 ),
    ( "WriteABORT",                 b"\x08\x00\x01\x02\x03\x04",    b"\x08\x00"                 ),
    ( "Delay",                      b"\x09\x01\x02\x03\x04",        b"\x09\x00"                 ),
    ( "ResetTarget",                b"\x0A" ,                       b"\x0A\x00\x00"             ),
    ( "DAP_SWJ_Pins",               b"\x10\x17\x17\x00\01\x02\x03", b"\x10\x99"                 ),
    ( "DAP_SWJ_Clock",              b"\x11\x00\x01\x02\x03",        b"\x11\x00"                 ),
    ( "DAP_SWJ_Sequence",           b"\x12\x20\x01\x02\x03\x04",    b"\x12\x00"                 ),
    ( "DAP_SWJ_Sequence (Long)",    b"\x12\xf8\x01\x02\x03\x04\x01\x02\x03\x04\x01\x02\x03\x04\x01\x02\x03\x04\x01\x02\x03\x04\x01\x02\x03\x04\x01\x02\x03\x04\x01\x02\x03",    b"\x12\x00"                 ),    
    ( "Vendor ID",                  b"\x00\x01",                    b"\x00\x00"                 ),
)

device = usb.core.find(idVendor=VENDOR_ID, idProduct=PRODUCT_ID)

if device is None:
    raise ValueError('Device not found. Please ensure it is connected')
    sys.exit(1)

# Claim interface 0 - this interface provides IN and OUT endpoints to write to and read from
u=usb.util.claim_interface(device, 0)
print("Interface claimed")

for desc,inseq,outsq in tests:
    print("==============",desc)
    write_to_usb(device,bytes(inseq))
    r=read_from_usb(device,1000)
    op_response(r,bytes(outsq))
