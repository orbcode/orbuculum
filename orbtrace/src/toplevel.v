

module topLevel(
		input [3:0] traceDin, // Port is always 4 bits wide, even if we use less
		input 	    traceClk, // Supporting clock for input - must be on a global clock pin

		input 	    uartrx, // Receive data into UART
		output 	    uarttx, // Transmit data from UART 

		// Leds....
		output 	    sync_led,
		output 	    rxInd_led, // Received UART Data indication
		output 	    txInd_led, // Transmitted UART Data indication
		output 	    txOvf_led,
		
		// Config and housekeeping
		input 	    clkIn,
		input 	    rst,

		// Other indicators
		output reg  D6,
		output reg  D5,
		output reg  D4,
		output reg  D3,
		output reg  cts,
		output 	    yellow,
		output 	    green   
		);
   

   // Parameters =============================================================================

   parameter MAX_BUS_WIDTH=4;  // Maximum bus width that system is set for 
   //    
   // Internals =============================================================================


   // DDR input data
   wire [MAX_BUS_WIDTH-1:0] tTraceDina;
   wire [MAX_BUS_WIDTH-1:0] tTraceDinb;
 		    
   wire 		    packetizer_avail_flag;
   wire 		    packetizer_strobe;
   wire 		    packetizer_word_strobe;
   wire [15:0] 		    packetizer_packet;

   wire 		   filter_strobe;
   wire [7:0] 		   filter_data;

   wire 		   doTransmit;
   wire 		   dataAvail;
   wire 		   txFree;
   
   wire [7:0]		   rx_byte_tl;
   wire 		   rxTrig_tl;
   wire 		   rxErr_tl;

   wire 		   lock; // Indicator that PLL has locked

   wire 		   clk;	

   
// Buffer for trace input clock
SB_GB_IO #(.PIN_TYPE(6'b000001)) BtraceClk0
(
  .PACKAGE_PIN(traceClk),
  .GLOBAL_BUFFER_OUTPUT(BtraceClk),
);

// Trace input pins config   
SB_IO #(.PULLUP(1)) MtraceIn0
(
 .PACKAGE_PIN (traceDin[0]),
 .INPUT_CLK (BtraceClk),
 .D_IN_0 (tTraceDina[0]),
 .D_IN_1 (tTraceDinb[0])
 );
   
SB_IO #(.PULLUP(1)) MtraceIn1
(
 .PACKAGE_PIN (traceDin[1]),
 .INPUT_CLK (BtraceClk),
 .D_IN_0 (tTraceDina[1]),
 .D_IN_1 (tTraceDinb[1])
  );
   
SB_IO #(.PULLUP(1)) MtraceIn2
(
 .PACKAGE_PIN (traceDin[2]),
 .INPUT_CLK (BtraceClk),
 .D_IN_0 (tTraceDina[2]),
 .D_IN_1 (tTraceDinb[2])
 );
   
SB_IO #(.PULLUP(1)) MtraceIn3 
(
 .PACKAGE_PIN (traceDin[3]),
 .INPUT_CLK (BtraceClk),
 .D_IN_0 (tTraceDina[3]),
 .D_IN_1 (tTraceDinb[3])
 );
   
   wire 		   target_a;
   wire 		   target_b;
   
   traceIF target (
                   .clk(clk), 
                   .rst(rst), 

		   // Downwards interface to trace pins
                   .traceDina(tTraceDina),       // Tracedata rising edge ... 1-n bits
                   .traceDinb(tTraceDinb),       // Tracedata falling edge (LSB) ... 1-n bits		   
                   .traceClkin(BtraceClk),       // Tracedata clock
		   .width(1),                    // Current trace buffer width 

		   // Upwards interface to packet processor
		   .PacketAvail(packetizer_avail_flag),   // Flag indicating packet is available
		   .PacketNext(packetizer_strobe),        // Strobe for requesting next packet
		   .PacketNextWd(packetizer_word_strobe), // Strobe for next 16 bit word in packet	 

		   .PacketOut(packetizer_packet),         // The next packet word

		   .sync(sync_led),                       // Indicator of if we are in sync
		   
		   .Probe(target_a),
		   .Probe2(target_b)
                   );

   assign green=target_a; // Ch 0
   assign yellow=target_b; // Ch 1
   

   packSend splitter (
		   .clk(clk), 
		   .rst(rst), 

		   .sync(sync_led), // Indicator of if we are in sync

		   // Downwards interface to target interface
		   .PacketAvail(packetizer_avail_flag),     // Flag indicating packet is available
		   .PacketNext(packetizer_strobe),          // Strobe for requesting next packet
		   .PacketNextWd(packetizer_word_strobe),   // Strobe for next 16 bit word in packet	 
		   .PacketIn(packetizer_packet),            // The next packet word

		   // Upwards interface to serial (or other) handler
		   .DataAvail(dataAvail),                   // There is decoded data ready for processing
		   .DataVal(filter_data),                   // Output data value
		   .DataNext(doTransmit),                   // Request for data

                   .DataOverf(txOvf_led),                   // Too much data in buffer
		   .Probe(filter_a)
 		      );
   
   assign doTransmit = dataAvail & txFree;

   uart #(.CLOCKFRQ(48_000_000))  receiver (
	.clk(clk),                 // System Clock
	.rst(rst),                 // System Reset
	.rx(uartrx),               // Uart TX pin
	.tx(uarttx),               // Uart RX pin
					  
	.transmit(doTransmit),
        .tx_byte(filter_data),
        .tx_free(txFree),
					  
	.received(rxTrig_tl),
	.rx_byte(rx_byte_tl),
					 
	.recv_error(rxErr_tl),

	.is_receiving(rxInd_led),
	.is_transmitting(txInd_led)
    );

 // Set up clock for 48Mhz with input of 12MHz
   SB_PLL40_CORE #(
		   .FEEDBACK_PATH("SIMPLE"),
		   .PLLOUT_SELECT("GENCLK"),
		   .DIVR(4'b0000),
		   .DIVF(7'b0111111),
		   .DIVQ(3'b100),
		   .FILTER_RANGE(3'b001)
		   ) uut (
			  .LOCK(lock),
			  .RESETB(1'b1),
			  .BYPASS(1'b0),
			  .REFERENCECLK(clkIn),
			  .PLLOUTCORE(clk)
			  );

   always @(negedge rst)
     begin
	D6<=1'b0;
	D5<=1'b0;
	D4<=1'b0;
	D3<=1'b0;
	cts<=1'b0;
     end
endmodule // topLevel
