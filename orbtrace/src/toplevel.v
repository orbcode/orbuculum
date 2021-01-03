`default_nettype none

module topLevel(
		input [3:0] 	  traceDin, // Port is always 4 bits wide, even if we use less
		input 		  traceClk, // Supporting clock for input - must be on a global clock pin

		input 		  uartrx, // Receive data into UART
		output 		  uarttx, // Transmit data from UART 

		// Leds....
		output 		  data_led,
		output 		  txInd_led, // Transmitted UART Data indication
		output 		  txOvf_led,
		output 		  heartbeat_led,
		
		// Config and housekeeping
		input 		  clkIn,
		input 		  rstIn,

		// Other indicators
		output reg 	  D3,
		output reg 	  D5,
		output reg 	  D6,
		output reg 	  D7,
		output reg 	  cts
				  
`ifdef INCLUDE_SUMP2
                , // Include SUMP2 connections
		input 		  uartrx,
		output 		  uarttx,
		input wire [15:0] events_din
`endif		
		);      

	    
   // Parameters =============================================================================

   parameter MAX_BUS_WIDTH=4;  // Maximum bus width that system is set for...not more than 4!! 

   // Internals =============================================================================


   wire 		   lock; // Indicator that PLL has locked
   wire 		   rst;
   wire 		   clk;
   wire 		   clkOut;
   wire 		   BtraceClk;

   reg                     ovf_strobe;

  
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
   
  // -----------------------------------------------------------------------------------------
  traceIF #(.MAXBUSWIDTH(MAX_BUS_WIDTH)) traceif (
                   .rst(rst), 

           // Downwards interface to trace pins
                   .traceDina(tTraceDina),          // Tracedata rising edge ... 1-n bits
                   .traceDinb(tTraceDinb),          // Tracedata falling edge (LSB) ... 1-n bits
                   .traceClkin(BtraceClk),          // Tracedata clock
		   .width(widthSet),                // Current trace buffer width 

           // Upwards interface to packet processor
		   .PkAvail(pkavail),               // Toggling flag indicating next packet
		   .Packet(packet)                  // The next packet
		);		  
   
  // -----------------------------------------------------------------------------------------

   wire [7:0] 		    tx_data;                // Data byte to be sent to serial

   wire 		    dataReady;              // Indicator that data is available
   
   wire 		    txFree;                 // Request for more data to tx
   
   wire [1:0] 		    widthSet;               // Current width of the interface
   reg                      txOvf_strobe;           // Strobe that data overflowed

   assign widthSet=2'b11;

   wire [127:0]             frame;
   wire                     frameNext;
   wire                     frameReady;
   
   packBuffer marshall (
		      .clk(clkOut), 
		      .rst(rst), 

           // Downwards interface to target interface
		      .PkAvail(pkavail),             // Flag indicating packet is available
		      .Packet(packet),               // The next packet

           // Upwards interface to output handler
		      .Frame(frame),                 // Output frame

		      .FrameNext(frameNext),         // Request for next data element
		      .FrameReady(frameReady),       // Next data element is available

           // Status indicators
                      .DataInd(data_led),            // LED indicating data sync status
                      .DataOverf(txOvf_strobe)       // Too much data in buffer
 		      );

  packToSerial serialPrepare (
		      .clk(clkOut),
		      .rst(rst),
		
	  // Downwards interface to packet buffer
		      .Frame(frame),                 // Input frame

		      .FrameNext(frameNext),         // Request for next data element
		      .FrameReady(frameReady),       // Next data element is available
                     
          // Upwards interface to serial handler
                      .DataVal(tx_data),             // Output data value
                      .DataReady(dataReady),
		      .DataNext(txFree&&!dataReady)  // Request for data
 		);
   
   uart #(.CLOCKFRQ(96_000_000), .BAUDRATE(12_000_000)) transmitter (
	             .clk(clkOut),                   // The master clock for this module
	             .rst(rst),                      // Synchronous reset.
	             .rx(uartrx),                    // Incoming serial line
	             .tx(uarttx),                    // Outgoing serial line
                     
	             .transmit(dataReady),           // Signal to transmit
	             .tx_byte(tx_data),              // Byte to transmit
	             .tx_free(txFree),               // Indicator that transmit register is available
	             .is_transmitting(txInd_led)     // Low when transmit line is idle.
                     );
   
 // Set up clock for 96Mhz with input of 12MHz
   SB_PLL40_CORE #(
		   .FEEDBACK_PATH("SIMPLE"),
		   .PLLOUT_SELECT("GENCLK"),
		   .DIVR(4'b0000),
		   .DIVF(7'b0111111),
		   //.DIVQ(3'b100),
                   .DIVQ(3'b011),
		   .FILTER_RANGE(3'b001)
		   ) uut (
			  .LOCK(lock),
			  .RESETB(1'b1),
			  .BYPASS(1'b0),
			  .REFERENCECLK(clkIn),
			  .PLLOUTCORE(clkOut)
			  );

// ========================================================================================================================

   reg [25:0] 		   clkCount;
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
	     D3<=0;
	     D5<=0;
	     D6<=0;
	     D7<=0;
	  end
	else
	  begin
             if (txOvf_strobe)
               ovfCount<=~0;
             else
               if (ovfCount>0) ovfCount<=ovfCount-1;

	     clkCount <= clkCount + 1;
	  end // else: !if(rst)
     end // always @ (posedge clkOut)

// ========================================================================================================================
// ========================================================================================================================
// ========================================================================================================================
// SUMP SETUP
// ========================================================================================================================
// ========================================================================================================================
// ========================================================================================================================
   
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
// ========================================================================================================================
// ========================================================================================================================
// ========================================================================================================================
// END OF SUMP2 SETUP
// ========================================================================================================================
// ========================================================================================================================
// ========================================================================================================================

endmodule // topLevel
