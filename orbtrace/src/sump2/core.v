/* ****************************************************************************
-- Source file: core.v                
-- Date:        October 15, 2016
-- Author:      khubbard
-- Description: Core wrapper for the logic. Minimized for fit to HX1K fabric.
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
-- 0.1   10.15.16  khubbard Creation
-- ***************************************************************************/
`default_nettype none // Strictly enforce all nets to be declared

module core 
(
  input  wire         reset,
  input  wire         clk_lb,   
  input  wire         clk_cap,  
  input  wire         lb_wr,
  input  wire         lb_rd,
  input  wire [23:0]  events_din,
  input  wire [31:0]  lb_addr,
  input  wire [31:0]  lb_wr_d,
  output wire [31:0]  lb_rd_d,
  output wire         lb_rd_rdy,
  output wire [3:0]   led_bus
);// module core


  wire [31:0]   u0_lb_rd_d;
  wire          u0_lb_rd_rdy;
  wire [31:0]   user_ctrl;
  wire          lb_cs_sump2_ctrl;
  wire          lb_cs_sump2_data;
//wire [31:0]   time_stamp_d;
//reg  [31:0]   lb_08_reg;
//reg  [15:0]   test_cnt;
//reg  [9:0]    h_cnt;
//reg  [9:0]    v_cnt;
//reg           h_sync;
//reg           h_valid;
//reg           v_sync;


// ----------------------------------------------------------------------------
// Test Design. VGA Controller
// 3 Build Choices that fit the HX1K
// o 16 bits from the Pins
// o 8 bits of pins or 8 test counter on user_ctrl[0] == 1
// o Sample graphics controller design ( 3 signals ).
// ----------------------------------------------------------------------------
//always @ ( posedge clk_cap ) begin : proc_test_cnt
//  if ( user_ctrl[0] == 0 ) begin
//    test_cnt[15:0] <= events_din[15:0];
//  end else begin
//    test_cnt[7:0]  <= test_cnt[7:0]  + 1;
//  end 
//  h_sync  <= 0;
//  h_valid <= 0;
//  if ( h_cnt == 10'd800 ) begin
//    h_cnt  <= 10'd1;
//    h_sync <= 1;
//  end else begin
//    h_valid <= 1;
//    h_cnt   <= h_cnt + 1;
//  end 
//  if ( h_sync == 1 ) begin
//    if ( v_cnt ==  10'd600 ) begin
//      v_cnt  <= 10'd1;
//      v_sync <= 1;
//    end else begin
//      v_sync <= 0;
//      v_cnt  <= v_cnt + 1;
//    end 
//  end
//end


// ----------------------------------------------------------------------------
// LocalBus Test Registers                                                 
// ----------------------------------------------------------------------------
//always @ ( posedge clk_lb or posedge reset ) begin : proc_name
// if ( reset == 1 ) begin
//   lb_rd_d    <= 32'd0;
//   lb_rd_rdy  <= 0;
//   lb_08_reg  <= 32'd0;
// end else begin
//   lb_rd_d    <= 32'd0;
//   lb_rd_rdy  <= 0;
//
//   if ( lb_wr == 1 && lb_addr[19:16] == 4'H0 ) begin
//     if ( lb_addr[15:0] == 16'h0008 ) begin
//       lb_08_reg[31:0] <= lb_wr_d[31:0];
//     end
//   end // if ( lb_wr == 1 )
//
//   if ( lb_rd == 1 && lb_addr[19:16] == 4'H0 ) begin
//     if ( lb_addr[15:0] == 16'h0000 ) begin
//       lb_rd_rdy     <= 1;
//       lb_rd_d[31:0] <= 32'h12345678;
//     end
//     if ( lb_addr[15:0] == 16'h0004 ) begin
//       lb_rd_rdy     <= 1;
//       lb_rd_d[31:0] <= time_stamp_d[31:0];
//     end
//     if ( lb_addr[15:0] == 16'h0008 ) begin
//       lb_rd_rdy     <= 1;
//       lb_rd_d[31:0] <= lb_08_reg[31:0];
//     end 
//   end // if ( lb_rd == 1 ) begin
//
// if ( u0_lb_rd_rdy == 1 ) begin
//   lb_rd_rdy <= 1;
//   lb_rd_d   <= u0_lb_rd_d[31:0];
// end
// end // clk+reset
//end // proc_name

//assign lb_cs_sump2_ctrl = ( lb_addr[3:0] == 4'h0 ) ? 1 : 0;
//assign lb_cs_sump2_data = ( lb_addr[3:0] == 4'h4 ) ? 1 : 0;
  assign lb_cs_sump2_ctrl = ~ lb_addr[2];// 0x0
  assign lb_cs_sump2_data =   lb_addr[2];// 0x4

  assign lb_rd_rdy = u0_lb_rd_rdy;
  assign lb_rd_d   = u0_lb_rd_d[31:0];


//-----------------------------------------------------------------------------
// 32bit UNIX TimeStamp of when the design was synthesized
//-----------------------------------------------------------------------------
//time_stamp u_time_stamp
//(
//  .time_dout                        ( time_stamp_d                   )
//);


//-----------------------------------------------------------------------------
// SUMP2 Example
//-----------------------------------------------------------------------------
sump2
#
(
  .depth_len      (  1024                   ),
  .depth_bits     (  10                     ),
  .event_bytes    (  2                      ),
  .data_dwords    (  0                      ),
  .nonrle_en      (  0                      ),
  .rle_en         (  1                      ),
  .pattern_en     (  0                      ),
  .trigger_nth_en (  0                      ),
  .trigger_dly_en (  0                      ),
  .trigger_wd_en  (  0                      ),
  .freq_mhz       (  16'd96                 ),
  .freq_fracts    (  16'h0000               )
)
u_sump2
(
  .reset         ( reset                    ),
  .clk_lb        ( clk_lb                   ),
  .clk_cap       ( clk_cap                  ),
  .lb_cs_ctrl    ( lb_cs_sump2_ctrl         ),
  .lb_cs_data    ( lb_cs_sump2_data         ),
  .lb_wr         ( lb_wr                    ),
  .lb_rd         ( lb_rd                    ),
  .lb_wr_d       ( lb_wr_d[31:0]            ),
  .lb_rd_d       ( u0_lb_rd_d               ),
  .lb_rd_rdy     ( u0_lb_rd_rdy             ),
  .active        (                          ),
  .trigger_in    ( 1'b0                     ),
  .trigger_out   (                          ),
  .events_din    ( {16'd0,events_din[15:0]} ),
  .dwords_3_0    ( 128'd0                   ),
  .dwords_7_4    ( 128'd0                   ),
  .dwords_11_8   ( 128'd0                   ),
  .dwords_15_12  ( 128'd0                   ),
  .led_bus       ( led_bus[3:0]             ),
  .user_ctrl     ( user_ctrl[31:0]          ),
  .user_pat0     (                          ),
  .user_pat1     (                          )
);


endmodule // core
