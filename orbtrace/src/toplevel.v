`default_nettype none

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
		input 	    rstIn,

		// Other indicators
		output reg  D6,
		output reg  D5,
		output reg  D4,
		output reg  D3,
		output reg  cts
//		,output 	    yellow
//		,output 	    green
//		,output      blue	    
		);      
	    
   // Parameters =============================================================================

   parameter MAX_BUS_WIDTH=4;  // Maximum bus width that system is set for...not more than 4!! 
   //    
   // Internals =============================================================================


   wire 		   lock; // Indicator that PLL has locked
   wire 		   rst;
   wire 		   clk;
   wire 		   clkOut;
   wire 		   BtraceClk;
  
`ifdef NO_GB_IO_AVAILABLE
// standard input pin for trace clock,
// then route it into an internal global buffer.
SB_GB BtraceClk0 (
 .USER_SIGNAL_TO_GLOBAL_BUFFER(traceClk),
 .GLOBAL_BUFFER_OUTPUT(BtraceClk)
 );
`else
// Buffer for trace input clock
SB_GB_IO #(.PIN_TYPE(6'b000001)) BtraceClk0
(
  .PACKAGE_PIN(traceClk),
  .GLOBAL_BUFFER_OUTPUT(BtraceClk)
);
`endif

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

   // DDR input data
   wire [MAX_BUS_WIDTH-1:0] tTraceDina;
   wire [MAX_BUS_WIDTH-1:0] tTraceDinb;
 		    
   wire 		    wclk;
   wire 		    wdavail;
   wire [15:0] 		    packetwd;
   wire 		    packetr;
   
  // -----------------------------------------------------------------------------------------
  traceIF #(.BUSWIDTH(MAX_BUS_WIDTH)) traceif (
                   .clk(clkOut), 
                   .rst(rst), 

		   // Downwards interface to trace pins
                   .traceDina(tTraceDina),       // Tracedata rising edge ... 1-n bits
                   .traceDinb(tTraceDinb),       // Tracedata falling edge (LSB) ... 1-n bits		   
                   .traceClkin(BtraceClk),       // Tracedata clock
		   .width(4),                    // Current trace buffer width 

		   // Upwards interface to packet processor
		   .WdAvail(wdavail),            // Flag indicating word is available
		   .PacketWd(packetwd),          // The next packet word
		   .PacketReset(packetr),        // Flag indicating to start again

   		   .sync(sync_led)               // Indicator that we are in sync
		);		  
   
  // -----------------------------------------------------------------------------------------

   reg [7:0] 		    filter_data;

   wire 		    dataAvail;
   wire 		    dataReady;
   
   wire 		    txFree;
   
   wire [7:0] 		    rx_byte_tl;
   wire 		    rxTrig_tl;
   wire 		    rxErr_tl;

   
   packSend marshall (
		      .clk(clkOut), 
		      .rst(rst), 

		      .sync(sync_led), // Indicator of if we are in sync

		      // Downwards interface to target interface
		      .wrClk(BtraceClk),             // Clock for write side operations to fifo
		      .WdAvail(wdavail),             // Flag indicating word is available
		      .PacketReset(packetr),         // Flag indicating to start again
		      .PacketWd(packetwd),           // The next packet word
		      
		      // Upwards interface to serial (or other) handler
                      .DataReady(dataReady),
		      .DataVal(filter_data),         // Output data value
		      .DataNext(txFree),             // Request for data
		      
                      .DataOverf(txOvf_led)          // Too much data in buffer
 		      );
   // -----------------------------------------------------------------------------------------   
   uart #(.CLOCKFRQ(48_000_000), .BAUDRATE(12_000_000))  receiver (
	.clk(clkOut),                 // System Clock
	.rst(rst),                 // System Reset
	.rx(uartrx),               // Uart TX pin
	.tx(uarttx),               // Uart RX pin
					  
	.transmit(dataReady),
        .tx_byte(filter_data),
        .tx_free(txFree),
					  
	.received(rxTrig_tl),
	.rx_byte(rx_byte_tl),
					 
	.recv_error(rxErr_tl),

	.is_receiving(rxInd_led),
	.is_transmitting(txInd_led)
    );

  // -----------------------------------------------------------------------------------------   
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
			  .PLLOUTCORE(clkOut)
			  );

   reg [25:0] 		   clkCount;

   // We don't want anything awake until the clocks are stable
   assign rst=(lock&rstIn);
   
   always @(posedge clkOut)
     begin
	if (rst)
	  begin
	     D5<=1'b0;
	     D4<=1'b0;
	     D3<=1'b0;
	     D6<=1'b0;
	     cts<=1'b0;
	     clkCount <= 0;
	  end
	else
	  begin	  
	     clkCount <= clkCount + 1;
	     D6<=clkCount[25];
	  end // else: !if(rst)
     end
endmodule // topLevel
