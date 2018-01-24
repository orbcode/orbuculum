/* ****************************************************************************
-- Source file: top.v                
-- Date:        October 08, 2016
-- Author:      khubbard
-- Description: Top Level Verilog RTL for Lattice ICE5LP FPGA Design
-- Language:    Verilog-2001 and VHDL-1993
-- Simulation:  Mentor-Modelsim 
-- Synthesis:   Lattice     
-- License:     This project is licensed with the CERN Open Hardware Licence
--              v1.2.  You may redistribute and modify this project under the
--              terms of the CERN OHL v.1.2. (http://ohwr.org/cernohl).
--              This project is distributed WITHOUT ANY EXPRESS OR IMPLIED
--              WARRANTY, INCLUDING OF MERCHANTABILITY, SATISFACTORY QUALITY
--              AND FITNESS FOR A PARTICULAR PURPOSE. Please see the CERN OHL
--              v.1.2 for applicable Conditions.
-- 
--  Lattice ICE-Stick
--               -----------------------------------------------------------
--             /                   119 118 117 116 115 114 113 112 GND  3V  |
--            /                      o   o   o   o   o   o   o   o   o   o  |
--           /                                               -----          |
--   -------             ---------          R            3V |o   o|3V       |
--  |          -------  |         |         |           GND |o P o|GND      |
--  |USB      | FTDI  | |Lattice  |      R--G--R    event[7]|o M o|event[6] |
--  |         |FT2232H| |iCE40HX1K|         |       event[5]|o O o|event[4] |
--  |          -------  |         |         R       event[3]|o D o|event[2] |
--   -------             ---------                  event[1]|o   o|event[0] |
--           \                                               -----          |
--            \                      o   o   o   o   o   o   o   o   o   o  |
--             \                    44  45  47  48  56  60  61  62  GND 3V  |
--               ----------------------------------------------------------- 
--                           event [15][14][13][12][11][10] [9] [8]
--
-- Revision History:
-- Ver#  When      Who      What
-- ----  --------  -------- ---------------------------------------------------
-- 0.1   10.08.16  khubbard Creation
-- ***************************************************************************/
`default_nettype none // Strictly enforce all nets to be declared

module top
(
  input  wire         clk_12m,
  input  wire         ftdi_wi,
  output wire         ftdi_ro,
  input  wire [7:0]   events_din,

  input  wire         p44,
  input  wire         p45,
  input  wire         p47,
  input  wire         p48,
  input  wire         p56,
  input  wire         p60,
  input  wire         p61,
  input  wire         p62,

  output wire         p119,    
  output wire         p118,    
  output wire         p117,    
  output wire         p116,    
  output wire         p115,    
  output wire         p114,    
  output wire         p113,    
  output wire         p112,    

  input  wire         p128,    

  output wire         spi_sck,
  output wire         spi_cs_l,
  output wire         spi_mosi,
  input  wire         spi_miso,

  output wire         ir_sd,
  output wire         ir_txd,
  input  wire         ir_rxd,

  output wire         LED1,
  output wire         LED2,
  output wire         LED3,
  output wire         LED4,
  output wire         LED5
);// module top

  wire          lb_wr;
  wire          lb_rd;
  wire [31:0]   lb_addr;
  wire [31:0]   lb_wr_d;
  wire [31:0]   lb_rd_d;
  wire          lb_rd_rdy;
  wire [23:0]   events_loc;

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
  wire [3:0]    mesa_wi_nib_d;
  wire          mesa_wo_byte_en;
  wire [7:0]    mesa_wo_byte_d;
  wire          mesa_wo_busy;
  wire          mesa_ro_byte_en;
  wire [7:0]    mesa_ro_byte_d;
  wire          mesa_ro_busy;
  wire          mesa_ro_done;
  wire [7:0]    mesa_core_ro_byte_d;
  wire          mesa_core_ro_byte_en;
  wire          mesa_core_ro_done;
  wire          mesa_core_ro_busy;


  wire          mesa_wi_baudlock;
  wire [3:0]    led_bus;
  reg  [7:0]    test_cnt;
  reg           ck_togl;

  assign LED1 = led_bus[0];
  assign LED2 = led_bus[1];
  assign LED3 = led_bus[2];
  assign LED4 = led_bus[3];
  assign LED5 = 1'b1;// Green LED always ON. Not enough resources to flash

  assign ir_sd  = 1'b1;// 1==Shutdown 0==ON
  assign ir_txd = 1'b0;

  assign reset_loc = 0;
//assign reset_core = ~ pll_lock;// didn't fit

  // Hookup FTDI RX and TX pins to MesaBus Phy
  assign mesa_wi_loc = ftdi_wi;
  assign ftdi_ro     = mesa_ro_loc;


  assign events_loc[7:0]   = events_din[7:0];
  assign events_loc[15:8]  = { p44,p45,p47,p48,p56,p60,p61,p62 };
//assign events_loc[23:16] = { p119,p118,p117,p116,p115,p114,p113,p112 };
  assign events_loc[23:16] = 8'd0;// Didn't fit


//-----------------------------------------------------------------------------
// PLL generated by Lattice GUI to multiply 12 MHz to 96 MHz
// PLL's RESET port is active low. How messed up of a signal name is that?
//-----------------------------------------------------------------------------
top_pll u_top_pll
(
  .REFERENCECLK ( clk_12m     ),
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
//assign p119 = test_cnt[7];
//assign p118 = test_cnt[6];
//assign p117 = test_cnt[5];
//assign p116 = test_cnt[4];
//assign p115 = test_cnt[3];
//assign p114 = test_cnt[2];
//assign p113 = test_cnt[1];
//assign p112 = test_cnt[0];
  
  assign p119 = test_cnt[5];
  assign p118 = test_cnt[4];
  assign p117 = test_cnt[3];
  assign p116 = test_cnt[2];
  assign p115 = test_cnt[1];
  assign p114 = test_cnt[0];
//assign p116 = lb_rd_rdy;
//assign p115 = lb_rd;
//assign p114 = lb_wr;

//assign p119 = 1'b0;
//assign p118 = 1'b0;
//assign p117 = 1'b0;
//assign p116 = 1'b0;
//assign p115 = 1'b0;
//assign p114 = 1'b0;
  assign p113 = mesa_ro_loc;
  assign p112 = ftdi_wi;


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
  .spi_sck             ( spi_sck                  ),
  .spi_cs_l            ( spi_cs_l                 ),
  .spi_mosi            ( spi_mosi                 ),
  .spi_miso            ( spi_miso                 ),
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


endmodule // top.v
