/* ****************************************************************************
-- (C) Copyright 2015 Black Mesa Labs
-- Source file: mesa_tx_uart.v                
-- Date:        June 1, 2015   
-- Author:      khubbard
-- Description: TX only 1/2 of UART for transmitting Wo bytes.
-- Language:    Verilog-2001 
-- License:     This project is licensed with the CERN Open Hardware Licence
--              v1.2.  You may redistribute and modify this project under the
--              terms of the CERN OHL v.1.2. (http://ohwr.org/cernohl).
--              This project is distributed WITHOUT ANY EXPRESS OR IMPLIED
--              WARRANTY, INCLUDING OF MERCHANTABILITY, SATISFACTORY QUALITY
--              AND FITNESS FOR A PARTICULAR PURPOSE. Please see the CERN OHL
--              v.1.2 for applicable Conditions.
--
-- RXD    \START/<D0><D1><D2><..><D7>/STOP
-- Design Statistics after Packing
-- en_clr_lock = 1;
--    Number of LUTs  : 190 / 384
--    Number of DFFs  : 110 / 384
--
-- Revision History:
-- Ver#  When      Who      What
-- ----  --------  -------- ---------------------------------------------------
-- 0.1   06.01.15  khubbard Creation
-- ***************************************************************************/
//`default_nettype none // Strictly enforce all nets to be declared
                                                                                
module mesa_tx_uart
(
  input  wire         reset,
  input  wire         clk,
  input  wire [7:0]   tx_byte,
  input  wire         tx_en,
  output reg          tx_busy,
  output reg          txd,
  input  wire         baud_lock,
  input  wire [15:0]  baud_rate
); // module mesa_tx_uart


  reg  [15:0]   tx_cnt_16b;
  reg  [3:0]    tx_bit_cnt;
  reg  [9:0]    tx_sr;
  reg           txd_loc;
  reg           tx_shift;
  reg           tx_now;
  reg           tx_en_p1;
  reg           baud_lock_p1;
  reg           send_lf;


//-----------------------------------------------------------------------------
// TX : Load 8bits into 10bit SR and shift out at the RX baud rate    
//-----------------------------------------------------------------------------
always @ ( posedge clk ) begin : proc_tx 
 begin
  tx_shift     <= 0;
  tx_busy      <= 0;
  txd          <= txd_loc;
  txd_loc      <= tx_sr[0];
  tx_now       <= 0;
  tx_en_p1     <= tx_en;
  baud_lock_p1 <= baud_lock;   
  send_lf      <= baud_lock & ~ baud_lock_p1;

  // Load a new Byte to send
  if ( ( tx_en == 1 && tx_en_p1 == 0 ) || ( send_lf == 1 )  ) begin
    tx_bit_cnt <= 4'd10;
    tx_cnt_16b <= 16'h0000;
    tx_busy    <= 1;
    tx_shift   <= 1;
    tx_sr[9:0] <= { 1'b1, tx_byte[7:0], 1'b0 };
    if ( send_lf == 1 ) begin
      tx_sr[9:0] <= { 1'b1, 8'h0A , 1'b0 };// Used for autobauding next node
    end 

  // Shift and send the byte until bit_cnt goes to 0
  end else if ( tx_bit_cnt != 4'd0 ) begin
    tx_cnt_16b <= tx_cnt_16b + 1;
    tx_busy    <= 1;
    if ( tx_now == 1 ) begin
      tx_shift   <= 1;
      tx_bit_cnt <= tx_bit_cnt[3:0] - 1;
      tx_cnt_16b <= 16'h0000;
      tx_sr[9:0] <= { 1'b1, tx_sr[9:1] };
    end
  end else begin
    tx_sr <= 10'H3FF;
  end

  if ( tx_cnt_16b[15:0] == baud_rate[15:0] ) begin
    tx_now <= 1;
  end

  if ( reset == 1 ) begin
    tx_bit_cnt <= 4'd0;
  end

 end
end // proc_tx


endmodule // mesa_tx_uart
