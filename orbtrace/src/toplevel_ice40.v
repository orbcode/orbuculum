`default_nettype none

module topLevel(
		input [3:0]       traceDin, // Port is always 4 bits wide, even if we use less
		input             traceClk, // Supporting clock for input - global clock pin
		input             rstIn,

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

		// Leds....
		output            data_led,
		output            txInd_led, // Transmitted UART Data indication
		output            txOvf_led,
		output            heartbeat_led,
		
		// Config and housekeeping
		input             clkIn,


		// Other indicators
`ifndef ICEBREAKER                 
		output reg        D3,
		output reg        D5,
		output reg        D6,
		output reg        D7,
`endif
		output reg        cts
				  
                                  `ifdef INCLUDE_SUMP2
                                  , // Include SUMP2 connections
		input             uartrx,
		output            uarttx,
		input wire [15:0] events_din
                                  `endif
		);      
   // Parameters =============================================================================

   parameter MAX_BUS_WIDTH=4;  // Maximum bus width that system is set for...not more than 4!! 
   parameter BUFFLENLOG2=9;    // Depth of packet frame buffer (512 bytes)
   parameter UART_BAUDRATE=12_000_000;
`ifndef ICEBREAKER
   parameter SYSTEM_CLK_MHZ=96_000_000;
   parameter TRACEIF_SYNC_BITS=27;
`else
   parameter SYSTEM_CLK_MHZ=48_000_000;
   parameter TRACEIF_SYNC_BITS=28;
`endif
   // Internals =============================================================================

                                  `ifdef ICEBREAKER
   reg                            D5;          // Fake wire for inversion
   
                                  `endif
   
   wire 		   lock;               // Indicator that PLL has locked
   wire 		   rst;
   wire 		   clk;
   wire 		   clkOut;             // Buffered clock for use across system
   wire 		   BtraceClk;          // Buffered trace input clock

   reg                     ovf_strobe;         // Strobe indicating overflow condition

`ifdef NO_GB_IO_AVAILABLE
// standard input pin for trace clock,
// then route it into an internal global buffer.
SB_GB BtraceClk0 (
 .USER_SIGNAL_TO_GLOBAL_BUFFER(traceClk),
 .GLOBAL_BUFFER_OUTPUT(BtraceClk)
 );
`else
// Buffer for trace input clock
SB_GB_IO #(.PIN_TYPE(6'b000000)) BtraceClk0
(
  .PACKAGE_PIN(traceClk),
  .GLOBAL_BUFFER_OUTPUT(BtraceClk)
);
`endif

// Trace input pins config   
SB_IO #(.PULLUP(1), .PIN_TYPE(6'b0)) MtraceIn0
(
 .PACKAGE_PIN (traceDin[0]),
 .INPUT_CLK (BtraceClk),
 .D_IN_0 (tTraceDina[0]),
 .D_IN_1 (tTraceDinb[0])
 );
   
SB_IO #(.PULLUP(1), .PIN_TYPE(6'b0)) MtraceIn1
(
 .PACKAGE_PIN (traceDin[1]),
 .INPUT_CLK (BtraceClk),
 .D_IN_0 (tTraceDina[1]),
 .D_IN_1 (tTraceDinb[1])
  );
   
SB_IO #(.PULLUP(1), .PIN_TYPE(6'b0)) MtraceIn2
(
 .PACKAGE_PIN (traceDin[2]),
 .INPUT_CLK (BtraceClk),
 .D_IN_0 (tTraceDina[2]),
 .D_IN_1 (tTraceDinb[2])
 );
   
SB_IO #(.PULLUP(1), .PIN_TYPE(6'b0)) MtraceIn3 
(
 .PACKAGE_PIN (traceDin[3]),
 .INPUT_CLK (BtraceClk),
 .D_IN_0 (tTraceDina[3]),
 .D_IN_1 (tTraceDinb[3])
 );

   // DDR input data
   wire [MAX_BUS_WIDTH-1:0] tTraceDina;           // Low order bits (first edge) of input
   wire [MAX_BUS_WIDTH-1:0] tTraceDinb;           // High order bits (second edge) of input

   wire 		    pkavail;              // Toggle of packet availability
   wire [127:0] 	    packet;               // Packet delivered from trace interface
   reg  [1:0] 		    widthSet;             // Current width of the interface
   wire [1:0]               widthCmd;             // Commanded width

   wire [15:0]              lostFrames;           // Number of frames lost due to lack of space
   wire [31:0]              totalFrames;          // Number of frames received overall
   
  // -----------------------------------------------------------------------------------------
  traceIF #(.MAXBUSWIDTH(MAX_BUS_WIDTH),.SYNC_BITS(TRACEIF_SYNC_BITS)) traceif (
                   .rst(rst), 

           // Downwards interface to trace pins
                   .traceDina(tTraceDina),          // Tracedata rising edge ... 1-n bits
                   .traceDinb(tTraceDinb),          // Tracedata falling edge (LSB) ... 1-n bits
                   .traceClkin(BtraceClk),          // Tracedata clock
		   .width(widthSet),                // Current trace buffer width 
                   .edgeOutput(D5),                 // DIAGNOSTIC for inverted sync

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
   wire [7:0]               leds  = { heartbeat_led, D5, txOvf_led, 1'b0, 1'b0, 1'b0, txInd_led, data_led };
   
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
                      .DataInd(data_led),            // LED indicating data sync status
                      .DataOverf(txOvf_strobe),      // Too much data in buffer

           // Stats
                      .TotalFrames(totalFrames),     // Number of frames received
                      .LostFrames(lostFrames)        // Number of frames lost
 		      );

   wire                     frameReady = (framesCnt != 0);

`ifdef USE_UART_TRANSPORT
   wire 		    txFree;                 // Request for more data to tx
   wire [7:0] 		    tx_data;                // Data byte to be sent to serial
   wire 		    dataReady;              // Indicator that data is available
   reg [7:0]                rxSerial;               // Input from serial
   wire                     rxedEvent;              // Indicator of a byte received

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
   
   uart #(.CLOCKFRQ(SYSTEM_CLK_MHZ), .BAUDRATE(UART_BAUDRATE)) transceiver (
	             .clk(clkOut),                   // The master clock for this module
	             .rst(rst),                      // Synchronous reset.
	             .rx(uartrx),                    // Incoming serial line
	             .tx(uarttx),                    // Outgoing serial line

                     .received(rxedEvent),           // Flag that something was received
                     .rx_byte(rxSerial),             // What we received
                     
	             .transmit(dataReady),           // Signal to transmit
	             .tx_byte(tx_data),              // Byte to transmit
	             .tx_free(txFree),               // Indicator that transmit register is available
	             .is_transmitting(txInd_led)     // Low when transmit line is idle.
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

                .Width(widthSet),                  // Trace port width
                .Transmitting(txInd_led),          // Indication of transmit activity

        // Downwards interface to packet buffer
		.Frame(frame),                     // Input frame
		.FrameNext(frameNext),             // Request for next data element
		.FrameReady(frameReady),           // Next data element is available
                .FramesCnt(framesCnt),             // No of frames available

	// Upwards interface to SPI
		.TxFrame(txFrame),                 // Output data frame
                .TxGetNext(txGetNext),             // Toggle to request next frame

		.RxPacket(rxPacket),               // Input data packet
		.PktComplete(pktComplete),         // Request for next data element
                .CS(SPIcs),                        // Current Chip Select state

        // Stats out
                .Leds(leds),                       // Led values on the board
                .TotalFrames(totalFrames),         // Number of frames received
                .LostFrames(lostFrames)            // Number of frames lost
	     );

    spi transceiver (
	        .rst(rst),
                .clk(clkOut),

	        .Tx(SPItx),                        // Outgoing SPI serial line (MISO)
                .Rx(SPIrx),                        // Incoming SPI serial line (MOSI)
                .Cs(SPIcs),
	        .DClk(SPIclk),                     // Clock for SPI

	        .Tx_packet(txFrame),               // Frame to transmit
                .TxGetNext(txGetNext),             // Toggle to request next frame

                .PktComplete(pktComplete),         // Toggle indicating data received
                .RxedFrame(rxPacket)               // The frame of data received
	    );
`endif

`ifdef ICEBREAKER
    // Fout = 12MHz * (0b0111111 + 1) / (2^0b100 * (0b0000 + 1)) = 48MHz
	SB_PLL40_2F_PAD #(
		.DIVR(4'b0000),
		.DIVF(7'b0111111),
		.DIVQ(3'b100),
		.FILTER_RANGE(3'b001),
		.FEEDBACK_PATH("SIMPLE"),
		.DELAY_ADJUSTMENT_MODE_FEEDBACK("FIXED"),
		.FDA_FEEDBACK(4'b0000),
		.SHIFTREG_DIV_MODE(2'b00),
		.PLLOUT_SELECT_PORTA("GENCLK"),
		.PLLOUT_SELECT_PORTB("GENCLK_HALF"),
		.ENABLE_ICEGATE_PORTA(1'b0),
		.ENABLE_ICEGATE_PORTB(1'b0)
	) pll_I (
		.PACKAGEPIN(clkIn),
		.PLLOUTCOREA(),
		.PLLOUTGLOBALA(clkOut),
		.EXTFEEDBACK(1'b0),
		.DYNAMICDELAY(8'h00),
		.RESETB(1'b1),
		.BYPASS(1'b0),
		.LATCHINPUTVALUE(1'b0),
		.LOCK(lock),
		.SDI(1'b0),
		.SDO(),
		.SCLK(1'b0)
	);
`else // !`ifdef ICEBREAKER
   // Setup for HX8 and HX1 parts
   // Set up clock for 96Mhz with input of 12MHz
   SB_PLL40_CORE #(
		   .FEEDBACK_PATH("SIMPLE"),
		   .PLLOUT_SELECT("GENCLK"),
		   .DIVR(4'b0000),
		   .DIVF(7'b0111111),
                   .DIVQ(3'b011),
		   .FILTER_RANGE(3'b001)
		   ) uut (
			  .LOCK(lock),
			  .RESETB(1'b1),
			  .BYPASS(1'b0),
			  .REFERENCECLK(clkIn),
			  .PLLOUTCORE(clkOut)
			  );
`endif
// ========================================================================================================================

   reg [25:0] 		   clkCount;       // Clock for heartbeat
   reg [23:0] 		   ovfCount;       // LED stretch for overflow indication

   assign heartbeat_led=clkCount[25];
   assign txOvf_led = (ovfCount!=0);

   // We don't want anything awake until the clocks are stable
   assign rst=(lock&rstIn);

   always @(posedge clkOut)
     begin
	if (rst)
	  begin
	     cts<=1'b0;
	     clkCount <= ~0;
             ovfCount <= 0;
`ifndef ICEBREAKER
	     D3<=0;
//	     D5<=0;                                    // Temporarily set from traceIF for low edge sync
	     D6<=0;
	     D7<=0;
`endif
	  end
	else
	  begin
             if (txOvf_strobe) ovfCount<=~0;
             else
               if (ovfCount>0) ovfCount<=ovfCount-1;

	     clkCount <= clkCount + 1;
	  end // else: !if(rst)
     end // always @ (posedge clkOut)

// =============================================================================================
// =============================================================================================
// =============================================================================================
// SUMP SETUP
// =============================================================================================
// =============================================================================================
// =============================================================================================
//
//    WARNING: This code has not been used for a while, and probably needs tidying.
//
   
`ifdef INCLUDE_SUMP2

   wire          lb_wr;
   wire          lb_rd;
   wire [31:0] 	 lb_addr;
   wire [31:0] 	 lb_wr_d;
   wire [31:0] 	 lb_rd_d;
   wire          lb_rd_rdy;
   wire [23:0] 	 events_loc;
   
   wire          clk_96m_loc;
   wire          clk_cap_tree;
   wire          clk_lb_tree;
   //wire          reset_core;
   wire          reset_loc;
   wire          pll_lock;
   
   wire          mesa_wi_loc;
   wire          mesa_wo_loc;
   wire          mesa_ri_loc;
   wire          mesa_ro_loc;
   
   wire          mesa_wi_nib_en;
   wire [3:0] 	 mesa_wi_nib_d;
   wire          mesa_wo_byte_en;
   wire [7:0] 	 mesa_wo_byte_d;
   wire          mesa_wo_busy;
   wire          mesa_ro_byte_en;
   wire [7:0] 	 mesa_ro_byte_d;
   wire          mesa_ro_busy;
   wire          mesa_ro_done;
   wire [7:0] 	 mesa_core_ro_byte_d;
   wire          mesa_core_ro_byte_en;
   wire          mesa_core_ro_done;
   wire          mesa_core_ro_busy;
   
   
   wire          mesa_wi_baudlock;
   wire [3:0] 	 led_bus;
   reg [7:0] 	 test_cnt;
   reg           ck_togl;
   
   wire 	 mesaspisck;
   wire 	 mesaspics;
   wire 	 mesaspimiso;
   wire 	 mesaspimosi;
   

   assign D5 = led_bus[0];
   assign D4 = led_bus[1];
   assign D3 = led_bus[2];
   assign D2 = led_bus[3];
   
   assign reset_loc = 0;
   //assign reset_core = ~ pll_lock;// didn't fit
   
   // Hookup FTDI RX and TX pins to MesaBus Phy
   assign mesa_wi_loc = uartrx;
   assign uarttx     = mesa_ro_loc;
   
   assign events_loc[3:0] = tTraceDina;
   assign events_loc[7:4] = tTraceDinb;   
   assign events_loc[8] = clkIn;
   assign events_loc[9] = txOvf_led;

   assign events_loc[10] = BtraceClk;
   assign events_loc[11] = wdavail;
   assign events_loc[12] = packetr;
   assign events_loc[13] = packetwd;
   
   assign events_loc[14] = dataReady;
//   assign events_loc[15] = txFree;
//   assign events_loc[11] = frameReset;

   assign events_loc[15] = data_led;
   
  
//   assign events_loc[7:0]   = events_din[7:0];
//   assign events_loc[15:8]  = events_din[15:8];
   //assign events_loc[23:16] = { p119,p118,p117,p116,p115,p114,p113,p112 };
   assign events_loc[23:16] = 8'd0;// Didn't fit
   
   
   //-----------------------------------------------------------------------------
// PLL generated by Lattice GUI to multiply 12 MHz to 96 MHz
// PLL's RESET port is active low. How messed up of a signal name is that?
//-----------------------------------------------------------------------------
   top_pll u_top_pll
     (
      .REFERENCECLK ( clkIn     ),
      .PLLOUTCORE   (             ),
      .PLLOUTGLOBAL ( clk_96m_loc ),
      .LOCK         ( pll_lock    ),
      .RESET        ( 1'b1        )
      );
   
   
   SB_GB u0_sb_gb 
     (
      //.USER_SIGNAL_TO_GLOBAL_BUFFER ( clk_12m      ),
      .USER_SIGNAL_TO_GLOBAL_BUFFER ( ck_togl      ),
      //.USER_SIGNAL_TO_GLOBAL_BUFFER ( clk_96m_loc  ),
      .GLOBAL_BUFFER_OUTPUT         ( clk_lb_tree  )
      );
   // Note: sump2.v modified to conserve resources requires single clock domain
   //assign clk_cap_tree = clk_lb_tree;

   SB_GB u1_sb_gb 
     (
      //.USER_SIGNAL_TO_GLOBAL_BUFFER ( ck_cap_togl  ),
      .USER_SIGNAL_TO_GLOBAL_BUFFER ( clk_96m_loc  ),
      .GLOBAL_BUFFER_OUTPUT         ( clk_cap_tree )
      );
   // assign clk_lb_tree = clk_12m;
   
   
   //-----------------------------------------------------------------------------
   // Note: 40kHz modulated ir_rxd signal looks like this
   //  \_____/                       \___/                      \___/
   //  |<2us>|<-------24us----------->
   //-----------------------------------------------------------------------------
   
   
   //-----------------------------------------------------------------------------
   // Toggle Flop To generate slower capture clocks.
   // 12MHz div-6  = 1 MHz toggle   1uS Sample
   // 12MHz div-48 = 125 kHz toggle 8uS Sample
   //-----------------------------------------------------------------------------
   //always @ ( posedge clk_12m ) begin : proc_div
   always @ ( posedge clk_cap_tree ) begin : proc_div
      begin
	 test_cnt <= test_cnt[7:0] + 1;
	 // ck_togl  <= ~ ck_togl;// 48 MHz
	 ck_togl  <= test_cnt[1];// 24 MHz
      end
   end // proc_div
   
   
   
   //-----------------------------------------------------------------------------
   // FSM for reporting ID : This also muxes in Ro Byte path from Core
   // This didn't fit in ICE-Stick, so removed.
   //-----------------------------------------------------------------------------
   //mesa_id u_mesa_id
   //(
   //  .reset                 ( reset_loc                ),
   //  .clk                   ( clk_lb_tree              ),
   //  .report_id             ( report_id                ),
   //  .id_mfr                ( 32'h00000001             ),
   //  .id_dev                ( 32'h00000002             ),
   //  .id_snum               ( 32'h00000001             ),
   //
   //  .mesa_core_ro_byte_en  ( mesa_core_ro_byte_en     ),
   //  .mesa_core_ro_byte_d   ( mesa_core_ro_byte_d[7:0] ),
   //  .mesa_core_ro_done     ( mesa_core_ro_done        ),
   //  .mesa_ro_byte_en       ( mesa_ro_byte_en          ),
   //  .mesa_ro_byte_d        ( mesa_ro_byte_d[7:0]      ),
   //  .mesa_ro_done          ( mesa_ro_done             ),
   //  .mesa_ro_busy          ( mesa_ro_busy             )
   //);// module mesa_id
   assign mesa_ro_byte_d[7:0] = mesa_core_ro_byte_d[7:0];
   assign mesa_ro_byte_en     = mesa_core_ro_byte_en;
   assign mesa_ro_done        = mesa_core_ro_done;
   assign mesa_core_ro_busy   = mesa_ro_busy;
   
   //-----------------------------------------------------------------------------
   // MesaBus Phy : Convert UART serial to/from binary for Mesa Bus Interface
   //  This translates between bits and bytes
   //-----------------------------------------------------------------------------
   mesa_phy u_mesa_phy
     (
      //.reset            ( reset_core          ),
      .reset            ( reset_loc           ),
      .clk              ( clk_lb_tree         ),
      .clr_baudlock     ( 1'b0                ),
      .disable_chain    ( 1'b1                ),
      .mesa_wi_baudlock ( mesa_wi_baudlock    ),
      .mesa_wi          ( mesa_wi_loc         ),
      .mesa_ro          ( mesa_ro_loc         ),
      .mesa_wo          ( mesa_wo_loc         ),
      .mesa_ri          ( mesa_ri_loc         ),
      .mesa_wi_nib_en   ( mesa_wi_nib_en      ),
      .mesa_wi_nib_d    ( mesa_wi_nib_d[3:0]  ),
      .mesa_wo_byte_en  ( mesa_wo_byte_en     ),
      .mesa_wo_byte_d   ( mesa_wo_byte_d[7:0] ),
      .mesa_wo_busy     ( mesa_wo_busy        ),
      .mesa_ro_byte_en  ( mesa_ro_byte_en     ),
      .mesa_ro_byte_d   ( mesa_ro_byte_d[7:0] ),
      .mesa_ro_busy     ( mesa_ro_busy        ),
      .mesa_ro_done     ( mesa_ro_done        )
      );// module mesa_phy
   
   
   //-----------------------------------------------------------------------------
   // MesaBus Core : Decode Slot,Subslot,Command Info and translate to LocalBus
   //-----------------------------------------------------------------------------
   mesa_core 
     #
     (
      .spi_prom_en       ( 1'b0                       )
      )
   
   u_mesa_core
     (
      //.reset               ( reset_core               ),
      .reset               ( ~mesa_wi_baudlock        ),
      .clk                 ( clk_lb_tree              ),
      .spi_sck             ( mesaspisck               ),
      .spi_cs_l            ( mesaspics                ),
      .spi_mosi            ( mesaspimosi              ),
      .spi_miso            ( mesaspimiso              ),
      .rx_in_d             ( mesa_wi_nib_d[3:0]       ),
      .rx_in_rdy           ( mesa_wi_nib_en           ),
      .tx_byte_d           ( mesa_core_ro_byte_d[7:0] ),
      .tx_byte_rdy         ( mesa_core_ro_byte_en     ),
      .tx_done             ( mesa_core_ro_done        ),
      .tx_busy             ( mesa_core_ro_busy        ),
      .tx_wo_byte          ( mesa_wo_byte_d[7:0]      ),
      .tx_wo_rdy           ( mesa_wo_byte_en          ),
      .subslot_ctrl        (                          ),
      .bist_req            (                          ),
      .reconfig_req        (                          ),
      .reconfig_addr       (                          ),
      .oob_en              ( 1'b0                     ),
      .oob_done            ( 1'b0                     ),
      .lb_wr               ( lb_wr                    ),
      .lb_rd               ( lb_rd                    ),
      .lb_wr_d             ( lb_wr_d[31:0]            ),
      .lb_addr             ( lb_addr[31:0]            ),
      .lb_rd_d             ( lb_rd_d[31:0]            ),
      .lb_rd_rdy           ( lb_rd_rdy                )
      );// module mesa_core
   
   
   //-----------------------------------------------------------------------------
   // Design Specific Logic
   //-----------------------------------------------------------------------------
   core u_core 
     (
      //.reset               ( reset_core               ),
      .reset               ( ~mesa_wi_baudlock        ),
      .clk_lb              ( clk_lb_tree              ),
      .clk_cap             ( clk_cap_tree             ),
      .lb_wr               ( lb_wr                    ),
      .lb_rd               ( lb_rd                    ),
      .lb_wr_d             ( lb_wr_d[31:0]            ),
      .lb_addr             ( lb_addr[31:0]            ),
      .lb_rd_d             ( lb_rd_d[31:0]            ),
      .lb_rd_rdy           ( lb_rd_rdy                ),
      .led_bus             ( led_bus[3:0]             ),
      .events_din          ( events_loc[23:0]         )
      );  

`endif
// =====================================================================================
// =====================================================================================
// =====================================================================================
// END OF SUMP2 SETUP
// =====================================================================================
// =====================================================================================
// =====================================================================================

endmodule // topLevel
