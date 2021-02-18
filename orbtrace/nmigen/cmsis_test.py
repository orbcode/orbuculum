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


device = usb.core.find(idVendor=VENDOR_ID, idProduct=PRODUCT_ID)

if device is None:
    raise ValueError('Device not found. Please ensure it is connected')
    sys.exit(1)

# Claim interface 0 - this interface provides IN and OUT endpoints to write to and read from
u=usb.util.claim_interface(device, 0)
print("Interface claimed")

print("\n=============Sending short request")
write_to_usb(device,bytes( {0x19,0x19} ))
r=read_from_usb(device,1000)
op_response(r,bytes( {0xff} ))

print("\n=============Sending request Vendor ID")
write_to_usb(device,bytes( [0x00,0x01] ))
r=read_from_usb(device,1000)
op_response(r, bytes( [0x00,0x00] ))

print("\n=============Sending request Product ID")
write_to_usb(device,bytes( [0x00,0x02] ))
r=read_from_usb(device,1000)
op_response(r, bytes( [0x00,0x00] ))

print("\n=============Sending request Serial Number")
write_to_usb(device,bytes( [0x00,0x03] ))
r=read_from_usb(device,1000)
op_response(r, bytes( [0x00,0x00] ))

print("\n=============Sending request Target Device Vendor")
write_to_usb(device,bytes( [0x00,0x05] ))
r=read_from_usb(device,1000)
op_response(r, bytes( [0x00,0x00] ))

print("\n=============Sending request Target Device Name")
write_to_usb(device,bytes( [0x00,0x06] ))
r=read_from_usb(device,1000)
op_response(r, bytes( [0x00,0x00] ))

print("\n=============Sending request fw version")
write_to_usb(device,bytes( [0x00,0x04] ))
r=read_from_usb(device,1000)
op_response(r, bytes( [0x00,0x04,0x31,0x2e,0x30,0x30] ))

print("\n=============Sending illegal command")
print("Wrote "+str(write_to_usb(device,bytes( {0x42} )))+" bytes")
r=read_from_usb(device,1000)
op_response(r, bytes( [0xfF] ))

print("\n=============Sending request CAPABILITIES")
write_to_usb(device,bytes( [0x00,0xf0] ))
r=read_from_usb(device,1000)
op_response(r, bytes( [0x00,0x01,0x01] ))

print("\n=============Sending request TEST DOMAIN TIMER")
write_to_usb(device,bytes( [0x00,0xf1] ))
r=read_from_usb(device,1000)
op_response(r, bytes( [0x00,0x08,0x00,0xca,0x9a,0x3b] ))

print("\n=============Sending request SWO Trace Buffer Size")
write_to_usb(device,bytes( [0x00,0xfD] ))
r=read_from_usb(device,1000)
op_response(r, bytes( [0x00,0x04,0xe8,0x03,0x00,0x00] ))

print("\n=============Sending request Packet Count")
write_to_usb(device,bytes( [0x00,0xfE] ))
r=read_from_usb(device,1000)
op_response(r, bytes( [0x00,0x01,0x40] ))

print("\n=============Sending request Packet Size")
write_to_usb(device,bytes( [0x00,0xff] ))
r=read_from_usb(device,1000)
op_response(r, bytes( [0x00,0x02,0x40,0x00] ))

print("\n=============Sending set connect led")
write_to_usb(device,bytes( [0x01,0x0,0x1] ))
r=read_from_usb(device,1000)
op_response(r, bytes( [0x01,0x00] ))

print("\n=============Sending set running led")
write_to_usb(device,bytes( [0x01,0x01,0x01] ))
r=read_from_usb(device,1000)
op_response(r, bytes( [0x01,0x00] ))

print("\n=============Sending set illegal led")
write_to_usb(device,bytes( [0x01,0x02,0x01] ))
r=read_from_usb(device,1000)
op_response(r, bytes( [0xff] ))

print("\n=============Sending connect swd")
write_to_usb(device,bytes( [0x02,0x01] ))
r=read_from_usb(device,1000)
op_response(r, bytes( [0x02,0x01] ))

print("\n=============Sending connect default")
write_to_usb(device,bytes( [0x02,0x00] ))
r=read_from_usb(device,1000)
op_response(r, bytes( [0x02,0x01] ))

print("\n=============Sending connect JTAG")
write_to_usb(device,bytes( [0x02,0x02] ))
r=read_from_usb(device,1000)
op_response(r, bytes( [0xff] ))

print("\n=============Sending disconnect")
write_to_usb(device,bytes( [0x03] ))
r=read_from_usb(device,1000)
op_response(r, bytes( [0x03,0x00] ))

print("\n=============Sending WriteABORT")
write_to_usb(device,bytes( [0x08,0x00,0x01,0x02,0x03,0x04] ))
r=read_from_usb(device,1000)
op_response(r, bytes( [0x08,0x00] ))

print("\n=============Sending Delay")
write_to_usb(device,bytes( [0x09,0x01,0x02,0x03,0x04] ))
r=read_from_usb(device,1000)
op_response(r, bytes( [0x09,0x00] ))

print("\n=============Sending ResetTarget")
write_to_usb(device,bytes( [0x0A] ))
r=read_from_usb(device,1000)
op_response(r, bytes( [0x0A,0x00,0x00] ))

