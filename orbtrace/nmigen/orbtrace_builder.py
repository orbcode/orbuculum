#!/usr/bin/env python3
#

import os
import itertools

from nmigen                  import *
from nmigen.build            import ResourceError
from nmigen.hdl              import ClockSignal

from nmigen.build.dsl        import Pins
from nmigen.hdl.xfrm         import DomainRenamer, ClockDomain
from usb_protocol.emitters   import DeviceDescriptorCollection


from luna.usb2               import USBDevice, USBMultibyteStreamInEndpoint, USBStreamInEndpoint, USBStreamOutEndpoint

from orbtrace_platform_ecp5  import orbtrace_ECPIX5_85_Platform
from traceIF                 import TRACE_TO_USB

from cmsis_dap               import CMSIS_DAP

# USB Endpoint configuration
TRACE_ENDPOINT_NUMBER         = 3
TRACE_ENDPOINT_SIZE           = 512

CMSIS_DAP_IN_ENDPOINT_NUMBER  = 1
CMSIS_DAP_OUT_ENDPOINT_NUMBER = 2
CMSIS_DAP_ENDPOINT_SIZE       = 512

# Length of various bit indicators
RX_LED_STRETCH_BITS   = 26
TX_LED_STRETCH_BITS   = 16
OVF_LED_STRETCH_BITS  = 26
HB_BITS               = 27

class OrbtraceDevice(Elaboratable):

    def create_descriptors(self):
        descriptors = DeviceDescriptorCollection()

        # We'll need a device descriptor...
        with descriptors.DeviceDescriptor() as d:
            d.idVendor           = 0x1209
            d.idProduct          = 0x3443  # Allocated from pid.codes

            d.iManufacturer      = "Orbcode"
            d.iProduct           = "Orbtrace with CMSIS-DAP"
            d.iSerialNumber      = "No.1"

            d.bNumConfigurations = 1


        # ... and a description of the USB configuration we'll provide.
        with descriptors.ConfigurationDescriptor() as c:
            with c.InterfaceDescriptor() as i:
                i._collection = descriptors
                i.bInterfaceNumber = 0
                i.iInterface = "CMSIS-DAP"

          #      with i.EndpointDescriptor() as e:
          #          e.bEndpointAddress = 0x80 | TRACE_ENDPOINT_NUMBER
          #          e.wMaxPacketSize   = TRACE_ENDPOINT_SIZE

                with i.EndpointDescriptor() as e:
                    e.bEndpointAddress = 0x80 | CMSIS_DAP_IN_ENDPOINT_NUMBER
                    e.wMaxPacketSize   = CMSIS_DAP_ENDPOINT_SIZE

                with i.EndpointDescriptor() as e:
                    e.bEndpointAddress = CMSIS_DAP_OUT_ENDPOINT_NUMBER
                    e.wMaxPacketSize   = CMSIS_DAP_ENDPOINT_SIZE
        return descriptors

    def elaborate(self, platform):
        self.rx_stretch  = Signal(RX_LED_STRETCH_BITS)
        self.tx_stretch  = Signal(TX_LED_STRETCH_BITS)
        self.ovf_stretch = Signal(OVF_LED_STRETCH_BITS)
        self.hb          = Signal(HB_BITS)

        # Individual LED conditions to be signalled
        self.hb_ind   = Signal()
        self.dat_ind  = Signal()
        self.ovf_ind  = Signal()
        self.tx_ind   = Signal()
        self.inv_ind  = Signal()
        self.leds_out = Signal(8)

        m = Module()

        # State of things to be reported via the USB link
        m.d.comb += self.leds_out.eq(Cat( self.dat_ind, self.tx_ind, C(0,3), self.ovf_ind, self.inv_ind, self.hb_ind ))

        def get_all_resources(name):
            resources = []
            for number in itertools.count():
                try:
                    resources.append(platform.request(name, number))
                except ResourceError:
                    break
            return resources

        # Generate our domain clocks/resets.
        m.submodules.car = platform.clock_domain_generator()

        # Create our USB device interface...
        ulpi = platform.request(platform.default_usb_connection)
        m.submodules.usb = usb = USBDevice(bus=ulpi)

        # Add our standard control endpoint to the device.
        descriptors = self.create_descriptors()
        usb.add_standard_control_endpoint(descriptors)

        # Add a stream endpoint to our device.
        trace_ep = USBMultibyteStreamInEndpoint(
            endpoint_number=TRACE_ENDPOINT_NUMBER,
            max_packet_size=TRACE_ENDPOINT_SIZE,
            byte_width=16
        )
        usb.add_endpoint(trace_ep)

        cmsisdapIn = USBStreamInEndpoint(
            endpoint_number=CMSIS_DAP_IN_ENDPOINT_NUMBER,
            max_packet_size=CMSIS_DAP_ENDPOINT_SIZE
        )
        usb.add_endpoint(cmsisdapIn)

        cmsisdapOut = USBStreamOutEndpoint(
            endpoint_number=CMSIS_DAP_OUT_ENDPOINT_NUMBER,
            max_packet_size=CMSIS_DAP_ENDPOINT_SIZE
        )
        usb.add_endpoint(cmsisdapOut)

        # Create a tracing instance
        tracepins=platform.request("tracein",0,xdr={"dat":2})
        m.submodules.trace = trace = TRACE_TO_USB(tracepins, trace_ep, self.leds_out)

        dbgpins = platform.request("dbgif",0)
        # Create a CMSIS DAP instance
        m.submodules.cmsisdap = cmsisdap = CMSIS_DAP( cmsisdapIn.stream, cmsisdapOut.stream, dbgpins )

        # Connect our device as a high speed device by default.
        m.d.comb += [
            usb.connect          .eq(1),
            usb.full_speed_only  .eq(0),
        ]

        # Run LED indicators - Heartbeat
        m.d.usb += self.hb.eq(self.hb-1)

        # Overflow
        with m.If((trace.overflow_ind) | (self.ovf_stretch!=0)):
            m.d.usb += self.ovf_stretch.eq(self.ovf_stretch-1)

        # Receive
        with m.If(trace.rx_ind):
            m.d.usb += self.rx_stretch.eq(~0)
        with m.Elif(self.rx_stretch!=0):
            m.d.usb += self.rx_stretch.eq(self.rx_stretch-1)

        # Transmit
        with m.If(trace.tx_ind):
            m.d.usb += self.tx_stretch.eq(~0)
        with m.Elif(self.tx_stretch!=0):
            m.d.usb += self.tx_stretch.eq(self.tx_stretch-1)

        # Map indicators to Actual LEDs; Rightmost to leftmost
        rgbled = [res for res in get_all_resources("rgb_led")]

        m.d.comb += [
            self.hb_ind.eq(self.hb[HB_BITS-1]==0),
            self.dat_ind.eq(self.rx_stretch!=0),
            self.tx_ind.eq(self.tx_stretch!=0),
            self.ovf_ind.eq(self.ovf_stretch!=0) ]

        m.d.comb += [ rgbled[0].r.o.eq(0),              rgbled[0].g.o.eq(self.hb_ind),                   rgbled[0].b.o.eq(0)            ]
        m.d.comb += [ rgbled[1].r.o.eq(self.ovf_ind),   rgbled[1].g.o.eq(self.tx_ind & ~self.ovf_ind),   rgbled[1].b.o.eq(0)            ]
        m.d.comb += [ rgbled[2].r.o.eq(trace.inv_ind),  rgbled[2].g.o.eq(0),                             rgbled[2].b.o.eq(0)            ]
        m.d.comb += [ rgbled[3].r.o.eq(0),              rgbled[3].g.o.eq(0),                             rgbled[3].b.o.eq(self.dat_ind) ]

        return m

if __name__ == "__main__":
    platform = orbtrace_ECPIX5_85_Platform()
    with open('../src/traceIF.v') as f:
        platform.add_file("traceIF.v",f)
    with open('../src/swdIF.v') as f:
        platform.add_file("swdIF.v",f)
    with open('../src/dbgIF.v') as f:
        platform.add_file("dbgIF.v",f)
    platform.build(OrbtraceDevice(), build_dir='build', do_program=True)

