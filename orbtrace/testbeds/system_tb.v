
module testbench;

		input [3:0] traceDin, // Port is always 4 bits wide, even if we use less
		input 	    traceClk, // Supporting clock
		input 	    uartrx, // Receive data into UART

		output 	    uarttx, // Transmit data from UART 


		output 	    sync_led,
		output 	    rxInd_led, // Received UART Data indication
		output 	    txInd_led, // Transmitted UART Data indication
		input 	    clkIn,
		input 	    rst,
		output 	    txOvf_led,
		output 	    D6,
		output 	    D5,
		output 	    D4,
		output 	    D3,
		output 	    cts,
		output 	    yellow,
		output      green   
		);
   

   // Parameters =============================================================================

   parameter MAX_BUS_WIDTH=4;  // Maximum bus width that system is set for 
   //    
   // Internals =============================================================================

   reg 			   nDv;

   reg 			   clk;

   wire [MAX_BUS_WIDTH-1:0] dOut_tl;        // ...the valid data
   wire [15:0] 		   packetizer_packet;
   wire [7:0] 		   packetizer_packet_final;
   wire [7:0] 		   filter_data;
   wire 		   target_strobe;
   wire 		   dvalid_tl;
   wire 		   packetizer_avail_flag;
   wire 		   packetizer_strobe;
   wire 		   packetizer_word_strobe;
   wire 		   filter_flag;
   wire 		   filter_strobe;
   wire 		   txTrig_tl;
   wire [7:0]		   rx_byte_tl;

   wire 		   rxTrig_tl;
		   
   wire 		   rxErr_tl;

   wire 		   lock; // Indicator that PLL has locked
   
   
   traceIF target (
                .clk(clk),                 // System Clock
                .rst(rst),                 // System Reset

                .traceDin(traceDin),       // Tracedata ... 1-n bits
                .traceClk(traceClk),       // Tracedata clock

		.dNext(target_strobe),     // Strobe for requesting next data element
		.dAvail(dvalid_tl),        // Flag indicating data is available
		.dOut(dOut_tl)             // ...the valid data 
                );

   packCollect packetizer (
		   .clk(clk), // System Clock
		   .rst(rst), // System reset
			   
		   .width(MAX_BUS_WIDTH), // Current trace buffer width
		   .sync(sync_led), // Indicator of if we are in sync

		   // Downwards interface to trace elements
		   .TraceNext(target_strobe), // Strobe for requesting next trace data element
		   .TraceAvail(dvalid_tl), // Flag indicating trace data is available
		   .TraceIn(dOut_tl), // ...the valid Trace Data

		   // Upwards interface to packet processor
		   .PacketAvail(packetizer_avail_flag), // Flag indicating packet is available
		   .PacketNext(packetizer_strobe), // Strobe for requesting next packet
		   .PacketNextWd(packetizer_word_strobe), // Strobe for next 16 bit word in packet	 

		   .PacketOut(packetizer_packet), // The next packet word
		   .PacketFinal(packetizer_packet_final) // The last word in the packet (used for reconstruction)
		   );

   wire 		   p;
   
   packFilter filter (
		   .clk(clk), // System Clock
		   .rst(rst), // System reset

		   .sync(sync_led), // Indicator of if we are in sync

		   // Downwards interface to packet processor
		   .PacketAvail(packetizer_avail_flag), // Flag indicating packet is available
		   .PacketNext(packetizer_strobe), // Strobe for requesting next packet
		   .PacketNextWd(packetizer_word_strobe), // Strobe for next 16 bit word in packet	 

		   .PacketIn(packetizer_packet), // The next packet word
		   .PacketFinal(packetizer_packet_final), // The last word in the packet (used for reconstruction)

		  // Upwards interface to serial (or other) handler
		   .DataAvail(filter_flag), // There is decoded data ready for processing
		   .DataVal(filter_data), //  	 Output data value
		   .DataReq(filter_strobe),     // Request for data
                   .DataOverf(txOvf_led),      // Too much data in buffer
		      .Probe(p)
 		      );
   

   uart #(.CLOCKFRQ(48000000))  receiver (
	.clk(clk),                 // System Clock
	.rst(rst),                 // System Reset
	.rx(uartrx),               // Uart TX pin
	.tx(uarttx),               // Uart RX pin
					  
	.transmit(filter_flag),
        .tx_byte(filter_data),
        .tx_done(filter_strobe),
					  
	.received(rxTrig_tl),
	.rx_byte(rx_byte_tl),
					 
	.recv_error(rxErr_tl),

	.is_receiving(rxInd_led),
	.is_transmitting(txInd_led)
    );

   assign txInd_led=filter_strobe;
  
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
   
   assign yellow=packetizer_avail_flag;
   assign green = p;
//packetizer_word_strobe;
//packetizer_word_strobe;
   
endmodule // topLevel
