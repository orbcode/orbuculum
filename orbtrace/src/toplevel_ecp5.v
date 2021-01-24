`default_nettype none

module topLevel(
                input             rstfpga,

                input [4:0]       PMOD7,    

`ifdef USE_UART_TRANSPORT
                // UART Connection
		input             uartrx, // Receive data into UART
		output            uarttx, // Transmit data from UART
`endif

`ifdef USE_SPI_TRANSPORT
                // SPI Connection
                output            SPItx,
                input             SPIrx,
                input             SPIclk,
                input             SPIcs,
`endif
                output [7:0]      PMOD4,
                
		// Leds....
                output [2:0]      led0,
                output [2:0]      led1,
                output [2:0]      led2,
                output [2:0]      led3, 
		
		// Config and housekeeping
		input             clkIn,
                                  
                                  `ifdef INCLUDE_SUMP2
                                  , // Include SUMP2 connections
		input             uartrx,
		output            uarttx,
		input wire [15:0] events_din
                                  `endif
		);      

   // Ports Mapping
   wire                           data_indic;          // Indication we have data
   wire                           tx_indic;            // Indication we are transmitting
   wire                           phaseinv_indic;      // Indication of phase inversion on rxed trace
   wire                           txOvf_indic;         // Indication of overflow
   wire                           hb_indic;            // Indication of heartbeat
   wire [3:0]                     traceDin=PMOD7[3:0]; // Port is always 4 bits wide, even if we use less
   wire                           traceClk=PMOD7[4];   // Supporting clock for input - global clock pin

   // Led indicator mapping (active low)
   //              Red                      Green                   Blue
   assign led0={!txOvf_indic,    (!tx_indic) | (txOvf_indic),       1'b1           };
   assign led1={rstfpga,                 !hb_indic,                 1'b1           };
   assign led2={  1'b1,                     1'b1,                !data_indic       };
   assign led3={  1'b1,                     1'b1,              !phaseinv_indic     };
  
   	    
   // Parameters =============================================================================

   parameter MAX_BUS_WIDTH=4;  // Maximum bus width that system is set for...not more than 4!! 
   parameter BUFFLENLOG2=9;    // Depth of packet frame buffer (512 bytes)

   // Internals =============================================================================

   wire 		   lock;               // Indicator that PLL has locked
   wire 		   clkOut;             // Buffered clock for use across system

// Trace input pins config 
//

IDDRX1F MtraceIn0
(
 .D (traceDin[0]),
 .SCLK (traceClk),
 .Q0 (tTraceDina[0]),
 .Q1 (tTraceDinb[0])
 );
   
IDDRX1F MtraceIn1
(
 .D (traceDin[1]),
 .SCLK (traceClk),
 .Q0 (tTraceDina[1]),
 .Q1 (tTraceDinb[1])
  );
   
IDDRX1F MtraceIn2
(
 .D (traceDin[2]),
 .SCLK (traceClk),
 .Q0 (tTraceDina[2]),
 .Q1 (tTraceDinb[2])
 );
   
IDDRX1F MtraceIn3 
(
 .D (traceDin[3]),
 .SCLK (traceClk),
 .Q0 (tTraceDina[3]),
 .Q1 (tTraceDinb[3])
 );

   
   // DDR input data
   wire [MAX_BUS_WIDTH-1:0] tTraceDina;           // Low order bits (first edge) of input
   wire [MAX_BUS_WIDTH-1:0] tTraceDinb;           // High order bits (second edge) of input

   wire 		    pkavail;              // Toggle of packet availability
   wire [127:0] 	    packet;               // Packet delivered from trace interface
   reg  [1:0] 		    widthSet;             // Current width of the interface

   wire [15:0]              lostFrames;           // Number of frames lost due to lack of space
   wire [31:0]              totalFrames;          // Number of frames received overall
   
  // -----------------------------------------------------------------------------------------
  traceIF #(.MAXBUSWIDTH(MAX_BUS_WIDTH)) traceif (
                   .rst(rst), 

           // Downwards interface to trace pins
                   .traceDina(tTraceDina),          // Tracedata rising edge ... 1-n bits
                   .traceDinb(tTraceDinb),          // Tracedata falling edge (LSB) ... 1-n bits
                   .traceClkin(traceClk),           // Tracedata clock
		   .width(widthSet),                // Current trace buffer width 
                   .edgeOutput(phaseinv_indic),     // DIAGNOSTIC for inverted sync

           // Upwards interface to packet processor
		   .FrAvail(pkavail),               // Toggling flag indicating next packet
		   .Frame(packet),                  // The next packet
		);		  
   
  // -----------------------------------------------------------------------------------------
   
   reg                      txOvf_strobe;           // Strobe that data overflowed

   wire [127:0]             frame;
   wire                     frameNext;
   wire [BUFFLENLOG2-1:0]   framesCnt;              // No of frames available
   wire                     frameReady;
   wire [7:0]               leds  = { hb_indic, phaseinv_indic, txOvf_indic, 1'b0, 1'b0, 1'b0, tx_indic, data_indic };
   
   frameBuffer #(.BUFFLENLOG2(BUFFLENLOG2)) marshall (
		      .clk(clkOut), 
		      .rst(rst), 

           // Downwards interface to target interface
		      .FrAvail(pkavail),             // Flag indicating frame is available
		      .FrameIn(packet),              // The input frame for storage

           // Upwards interface to output handler
		      .FrameOut(frame),              // Output frame

		      .FrameNext(frameNext),         // Request for next data element
		      .FramesCnt(framesCnt),         // Number of frames available

           // Status indicators
                      .DataInd(data_indic),          // LED indicating data sync status
                      .DataOverf(txOvf_strobe),      // Too much data in buffer

           // Stats
                      .TotalFrames(totalFrames),     // Number of frames received
                      .LostFrames(lostFrames)        // Number of frames lost
 		      );

   wire                     frameReady = (framesCnt != 0);

`ifdef USE_UART_TRANSPORT
   wire 		    txFree;                  // Request for more data to tx
   wire [7:0] 		    tx_data;                 // Data byte to be sent to serial
   wire 		    dataReady;               // Indicator that data is available
   reg [7:0]                rxSerial;                // Input from serial
   wire                     rxedEvent;               // Indicator of a byte received

  frameToSerial #(.BUFFLENLOG2(BUFFLENLOG2)) serialPrepare (
		      .clk(clkOut),
		      .rst(rst),
		
                      .Width(widthSet),              // Trace port width

	  // Downwards interface to packet buffer
		      .Frame(frame),                 // Input frame

		      .FrameNext(frameNext),         // Request for next data element
		      .FrameReady(frameReady),       // Next data element is available
                      .FramesCnt(framesCnt),         // No of frames available

          // Upwards interface to serial handler
                      .DataVal(tx_data),             // Output data value
                      .DataReady(dataReady),
		      .DataNext(txFree&&!dataReady), // Request for data
                      .RxedEvent(rxedEvent),         // Flag that something was received
                      .DataInSerial(rxSerial),       // Data we received over serial link

          // Stats out
                      .Leds(leds),                   // Led values on the board
                      .TotalFrames(totalFrames),     // Number of frames received
                      .LostFrames(lostFrames)        // Number of frames lost
 		);
   
   uart #(.CLOCKFRQ(192_000_000), .BAUDRATE(12_000_000)) transceiver (
	             .clk(clkOut),                   // The master clock for this module
	             .rst(rst),                      // Synchronous reset.
	             .rx(uartrx),                    // Incoming serial line
	             .tx(uarttx),                    // Outgoing serial line

                     .received(rxedEvent),           // Flag that something was received
                     .rx_byte(rxSerial),             // What we received
                     
	             .transmit(dataReady),           // Signal to transmit
	             .tx_byte(tx_data),              // Byte to transmit
	             .tx_free(txFree),               // Indicator that transmit register is available
	             .is_transmitting(tx_indic)      // Low when transmit line is idle.
                  );
`endif //  `ifdef USE_UART_TRANSPORT

`ifdef USE_SPI_TRANSPORT
   wire [BUFFLENLOG2-1:0]   framesCnt;
   wire [127:0]             txFrame;
   wire                     txGetNext;
   wire [31:0]              rxPacket;
   wire                     pktComplete;

   frameToSPI #(.BUFFLENLOG2(BUFFLENLOG2)) SPIPrepare (
		.clk(clkOut),
		.rst(rst),

                .Width(widthSet),                    // Trace port width
                .Transmitting(tx_indic),             // Indication of transmit activity

        // Downwards interface to packet buffer
		.Frame(frame),                       // Input frame
		.FrameNext(frameNext),               // Request for next data element
		.FrameReady(frameReady),             // Next data element is available
                .FramesCnt(framesCnt),               // No of frames available

	// Upwards interface to SPI
		.TxFrame(txFrame),                   // Output data frame
                .TxGetNext(txGetNext),               // Toggle to request next frame

		.RxPacket(rxPacket),                 // Input data packet
		.PktComplete(pktComplete),           // Request for next data element
                .CS(SPIcs),                          // Current Chip Select state

        // Stats out
                .Leds(leds),                         // Led values on the board
                .TotalFrames(totalFrames),           // Number of frames received
                .LostFrames(lostFrames)              // Number of frames lost
	     );

    spi transceiver (
	        .rst(rst),
                .clk(clkOut),

	        .Tx(SPItx),                          // Outgoing SPI serial line (MISO)
                .Rx(SPIrx),                          // Incoming SPI serial line (MOSI)
                .Cs(SPIcs),
	        .DClk(SPIclk),                       // Clock for SPI

	        .Tx_packet(txFrame),                 // Frame to transmit
                .TxGetNext(txGetNext),               // Toggle to request next frame

                .PktComplete(pktComplete),           // Toggle indicating data received
                .RxedFrame(rxPacket)                 // The frame of data received
	    );
`endif

(* FREQUENCY_PIN_CLKI="100" *)
(* FREQUENCY_PIN_CLKOP="192" *)
(* ICP_CURRENT="12" *) (* LPF_RESISTOR="8" *) (* MFG_ENABLE_FILTEROPAMP="1" *) (* MFG_GMCREF_SEL="2" *)
EHXPLLL #(
        .PLLRST_ENA("DISABLED"),
        .INTFB_WAKE("DISABLED"),
        .STDBY_ENABLE("DISABLED"),
        .DPHASE_SOURCE("DISABLED"),
        .OUTDIVIDER_MUXA("DIVA"),
        .OUTDIVIDER_MUXB("DIVB"),
        .OUTDIVIDER_MUXC("DIVC"),
        .OUTDIVIDER_MUXD("DIVD"),
        .CLKI_DIV(25),
        .CLKOP_ENABLE("ENABLED"),
        .CLKOP_DIV(3),
        .CLKOP_CPHASE(1),
        .CLKOP_FPHASE(0),
        .FEEDBK_PATH("CLKOP"),
        .CLKFB_DIV(48)
    ) pll_i (
        .RST(1'b0),
        .STDBY(1'b0),
        .CLKI(clkIn),
        .CLKOP(clkOut),
        .CLKFB(clkOut),
        .CLKINTFB(),
        .PHASESEL0(1'b0),
        .PHASESEL1(1'b0),
        .PHASEDIR(1'b1),
        .PHASESTEP(1'b1),
        .PHASELOADREG(1'b1),
        .PLLWAKESYNC(1'b0),
        .ENCLKOP(1'b0),
        .LOCK(lock)
	);
   
// ========================================================================================================================

   reg [27:0] 		   clkCount;       // Clock for heartbeat
   reg [23:0] 		   ovfCount;       // LED stretch for overflow indication

   assign hb_indic=clkCount[27];
   assign txOvf_indic = (ovfCount!=0);

   // We don't want anything awake until the clocks are stable
   wire                    rst=(!rstfpga || !lock);

   always @(posedge clkOut)
     begin
	if (rst)
	  begin
	     clkCount <= ~0;
             ovfCount <= 0;
	  end
	else
	  begin
             if (txOvf_strobe) ovfCount<=~0;
             else
               if (ovfCount>0) ovfCount<=ovfCount-1;

	     clkCount <= clkCount + 1;
	  end // else: !if(rst)
     end // always @ (posedge clkOut)

endmodule // topLevel
