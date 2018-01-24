/* ****************************************************************************
-- Source file: mesa_core.v                
-- Date:        October 4, 2015 
-- Author:      khubbard
-- Description: Wrapper around a bunch of Mesa Bus Modules. This takes in the
--              binary nibble stream from mesa_phy and takes care of slot
--              enumeration and subslot bus decoding to local-bus.
--              SubSlot-0 is 32bit user localbus.
--              SubSlot-E is 32bit SPI PROM Interface localbus.
--              SubSlot-F is power management,etc.
-- Language:    Verilog-2001 and VHDL-1993
-- Simulation:  Mentor-Modelsim 
-- Synthesis:   Xilinst-XST 
-- License:     This project is licensed with the CERN Open Hardware Licence
--              v1.2.  You may redistribute and modify this project under the
--              terms of the CERN OHL v.1.2. (http://ohwr.org/cernohl).
--              This project is distributed WITHOUT ANY EXPRESS OR IMPLIED
--              WARRANTY, INCLUDING OF MERCHANTABILITY, SATISFACTORY QUALITY
--              AND FITNESS FOR A PARTICULAR PURPOSE. Please see the CERN OHL
--              v.1.2 for applicable Conditions.
--
-- Revision History:
-- Ver#  When      Who      What
-- ----  --------  -------- ---------------------------------------------------
-- 0.1   10.04.15  khubbard Creation
-- ***************************************************************************/
`default_nettype none // Strictly enforce all nets to be declared
                                                                                
module mesa_core #
(
  parameter spi_prom_en = 1
)
(
  input  wire         clk,   
  input  wire         reset,
  output wire         lb_wr,
  output wire         lb_rd,
  output wire [31:0]  lb_addr,
  output wire [31:0]  lb_wr_d,
  input  wire [31:0]  lb_rd_d,
  input  wire         lb_rd_rdy,

  output wire         spi_sck,
  output wire         spi_cs_l,
  output wire         spi_mosi,
  input  wire         spi_miso,
  input  wire [3:0]   rx_in_d,
  input  wire         rx_in_rdy,
  output reg  [7:0]   tx_byte_d,
  output reg          tx_byte_rdy,
  output reg          tx_done,
  input  wire         tx_busy,
  output wire [7:0]   tx_wo_byte,
  output wire         tx_wo_rdy,
  input  wire         oob_en,
  input  wire         oob_done,

  output wire [8:0]   subslot_ctrl,
  output wire         reconfig_req,
  output wire [31:0]  reconfig_addr,
  output wire         bist_req
);// module mesa_core

  wire          rx_loc_rdy;
  wire          rx_loc_start;
  wire          rx_loc_stop;
  wire [7:0]    rx_loc_d;

  wire [7:0]    tx_spi_byte_d;
  wire          tx_spi_byte_rdy;
  wire          tx_spi_done;
  wire [7:0]    tx_lb_byte_d;
  wire          tx_lb_byte_rdy;
  wire          tx_lb_done;
  wire          tx_busy_loc;
  reg  [3:0]    tx_busy_sr;

  wire          prom_wr;
  wire          prom_rd;
  wire [31:0]   prom_addr;
  wire [31:0]   prom_wr_d;
  wire [31:0]   prom_rd_d;
  wire          prom_rd_rdy;
  reg           prom_cs_c;
  reg           prom_cs_d;

  wire [31:0]   slot_size;
  wire [31:0]   time_stamp_d;


//-----------------------------------------------------------------------------
// Decode the 2 PROM Addresses at 0x20 and 0x24 using combo logic
//-----------------------------------------------------------------------------
always @ ( * ) begin : proc_prom_decode
 begin
  if ( prom_addr[15:0] == 16'h0020 ) begin
    prom_cs_c <= prom_wr | prom_rd;
  end else begin
    prom_cs_c <= 0;
  end 
  if ( prom_addr[15:0] == 16'h0024 ) begin
    prom_cs_d <= prom_wr | prom_rd;
  end else begin
    prom_cs_d <= 0;
  end 
 end
end // proc_prom_decode


// ----------------------------------------------------------------------------
// Ro Mux : Mux between multiple byte sources for Ro readback path.
// Note: There is no arbitration - 1st come 1st service requires that only
// one device will send readback data ( polled requests ).
// ----------------------------------------------------------------------------
always @ ( posedge clk ) begin : proc_tx
  tx_busy_sr[0]   <= tx_lb_byte_rdy | tx_spi_byte_rdy | tx_busy;
  tx_busy_sr[3:1] <= tx_busy_sr[2:0];
  tx_byte_rdy     <= tx_lb_byte_rdy | tx_spi_byte_rdy;
  tx_done         <= tx_lb_done     | tx_spi_done | oob_done;// Sends LF

  if ( tx_lb_byte_rdy == 1 ) begin
    tx_byte_d <= tx_lb_byte_d[7:0];
  end else begin
    tx_byte_d <= tx_spi_byte_d[7:0];
  end 
end // proc_tx
  // Support pipeling Ro byte path by asserting busy for 4 clocks after a byte
  assign tx_busy_loc = ( tx_busy_sr != 4'b0000 ) ? 1 : 0;


//-----------------------------------------------------------------------------
// Decode Slot Addresses : Take in the Wi path as nibbles and generate the Wo
// paths for both internal and external devices.
//-----------------------------------------------------------------------------
mesa_decode u_mesa_decode
(
  .clk                              ( clk                            ),
  .reset                            ( reset                          ),
  .rx_in_d                          ( rx_in_d[3:0]                   ),
  .rx_in_rdy                        ( rx_in_rdy                      ),
  .rx_out_d                         ( tx_wo_byte[7:0]                ),
  .rx_out_rdy                       ( tx_wo_rdy                      ),
  .rx_loc_d                         ( rx_loc_d[7:0]                  ),
  .rx_loc_rdy                       ( rx_loc_rdy                     ),
  .rx_loc_start                     ( rx_loc_start                   ),
  .rx_loc_stop                      ( rx_loc_stop                    )
);


//-----------------------------------------------------------------------------
// Convert Subslots 0x0 and 0xE to 32bit local bus for user logic and prom 
//-----------------------------------------------------------------------------
mesa2lb u_mesa2lb
(
  .clk                              ( clk                            ),
  .reset                            ( reset                          ),
  .rx_byte_d                        ( rx_loc_d[7:0]                  ),
  .rx_byte_rdy                      ( rx_loc_rdy                     ),
  .rx_byte_start                    ( rx_loc_start                   ),
  .rx_byte_stop                     ( rx_loc_stop                    ),
  .tx_byte_d                        ( tx_lb_byte_d[7:0]              ),
  .tx_byte_rdy                      ( tx_lb_byte_rdy                 ),
  .tx_done                          ( tx_lb_done                     ),
  .tx_busy                          ( tx_busy_loc                    ),
  .lb_wr                            ( lb_wr                          ),
  .lb_rd                            ( lb_rd                          ),
  .lb_wr_d                          ( lb_wr_d[31:0]                  ),
  .lb_addr                          ( lb_addr[31:0]                  ),
  .lb_rd_d                          ( lb_rd_d[31:0]                  ),
  .lb_rd_rdy                        ( lb_rd_rdy                      ),
  .oob_en                           ( oob_en                         ),
  .oob_rd_d                         ( lb_rd_d[31:0]                  ),
  .oob_rd_rdy                       ( lb_rd_rdy                      ),
  .prom_wr                          ( prom_wr                        ),
  .prom_rd                          ( prom_rd                        ),
  .prom_wr_d                        ( prom_wr_d[31:0]                ),
  .prom_addr                        ( prom_addr[31:0]                ),
  .prom_rd_d                        ( prom_rd_d[31:0]                ),
  .prom_rd_rdy                      ( prom_rd_rdy                    )
);


//-----------------------------------------------------------------------------
// Convert Subslots 0x1 to SPI
// Use spi_ck_div of 0x7 for div-8 of 24 MHz to 3 MHz for SPI of 1.5 MHz.
//-----------------------------------------------------------------------------
//mesa2spi u_mesa2spi
//(
//  .clk                              ( clk                            ),
//  .reset                            ( reset                          ),
//  .subslot                          ( 4'd1                           ),
//  .spi_ck_div                       ( 4'd7                           ),
//  .rx_byte_d                        ( rx_loc_d[7:0]                  ),
//  .rx_byte_rdy                      ( rx_loc_rdy                     ),
//  .rx_byte_start                    ( rx_loc_start                   ),
//  .rx_byte_stop                     ( rx_loc_stop                    ),
//
//  .tx_byte_d                        ( tx_spi_byte_d[7:0]             ),
//  .tx_byte_rdy                      ( tx_spi_byte_rdy                ),
//  .tx_done                          ( tx_spi_done                    ),
//  .tx_busy                          ( tx_busy_loc                    ),
//  .spi_sck                          (                                ),
//  .spi_ss_l                         (                                ),
//  .spi_mosi                         (                                ),
//  .spi_miso                         (                                )
//);
  assign tx_spi_byte_d[7:0] = 8'd0;
  assign tx_spi_byte_rdy    = 1'b0;
  assign tx_spi_done        = 1'b0;


//-----------------------------------------------------------------------------
// Decode Subslot Nibble Controls
//-----------------------------------------------------------------------------
mesa2ctrl u_mesa2ctrl
(
  .clk                              ( clk                            ),
  .reset                            ( reset                          ),
  .rx_byte_d                        ( rx_loc_d[7:0]                  ),
  .rx_byte_rdy                      ( rx_loc_rdy                     ),
  .rx_byte_start                    ( rx_loc_start                   ),
  .rx_byte_stop                     ( rx_loc_stop                    ),
  .subslot_ctrl                     ( subslot_ctrl[8:0]              )
);


//-----------------------------------------------------------------------------
// 32bit UNIX TimeStamp of when the design was synthesized
//-----------------------------------------------------------------------------
time_stamp u_time_stamp
(
  .time_dout                        ( time_stamp_d                   )
);


//-----------------------------------------------------------------------------
// Interface to SPI PROM : Allow LB to program SPI PROM, request reconfig
// ck_divisor 10 is for ( 80M / 10 ) for 2x SPI Clock Rate of 4 MHz
// ck_divisor  3 is for ( 24M /  3 ) for 2x SPI Clock Rate of 4 MHz
// PROM Slot Size
//   slot_size 0x00020000 is 1Mbit Slot for ICE5LP4K
//   slot_size 0x00040000 is 2Mbit Slot for XC3S200A (Compressed, no BRAM ROMs)
//   slot_size 0x00080000 is 4Mbit Slot for XC6SLX9  (Compressed, no BRAM ROMs)
//-----------------------------------------------------------------------------
generate
if ( spi_prom_en == 1 ) begin
spi_prom u_spi_prom
(
  .reset                            ( reset                          ),
  .prom_is_32b                      ( 1'b0                           ),
  .ck_divisor                       ( 8'd3                           ),
  .slot_size                        ( slot_size[31:0]                ),
  .protect_1st_slot                 ( 1'b1                           ),
  .clk_lb                           ( clk                            ),

  .lb_cs_prom_c                     ( prom_cs_c                      ),
  .lb_cs_prom_d                     ( prom_cs_d                      ),
  .lb_wr                            ( prom_wr                        ),
  .lb_rd                            ( prom_rd                        ),
  .lb_wr_d                          ( prom_wr_d[31:0]                ),
  .lb_rd_d                          ( prom_rd_d[31:0]                ),
  .lb_rd_rdy                        ( prom_rd_rdy                    ),

  .spi_ctrl                         ( 4'b0000                        ),
  .spi_sck                          ( spi_sck                        ),
  .spi_cs_l                         ( spi_cs_l                       ),
  .spi_mosi                         ( spi_mosi                       ),
  .spi_miso                         ( spi_miso                       ),

  .flag_wip                         (                                ),
  .bist_req                         ( bist_req                       ),
  .reconfig_2nd_slot                ( 1'b0                           ),
  .reconfig_req                     ( reconfig_req                   ),
  .reconfig_addr                    ( reconfig_addr[31:0]            )
);// spi_prom
end else begin
  assign spi_sck       = 1;
  assign spi_cs_l      = 1;
  assign spi_mosi      = 1;
  assign bist_req      = 0;
  assign reconfig_req  = 0;
  assign reconfig_addr = 32'd0;
  assign prom_rd_d     = 32'd0;
  assign prom_rd_rdy   = 0;
end
endgenerate
  assign slot_size = 32'h00020000;


endmodule // mesa_core
