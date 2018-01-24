/* ****************************************************************************
-- Source file: mesa_byte2ascii.v                
-- Date:        October 4, 2015
-- Author:      khubbard
-- Description: Convert a binary byte to two ASCII nibs for UART transmission.
--              Must handshake with both byte sender and UART for busy status.
--              Also tracks a tx_done signal that queues up a "\n" to be sent.
-- Language:    Verilog-2001
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
-- Revision History:
-- Ver#  When      Who      What
-- ----  --------  -------- ---------------------------------------------------
-- 0.1   10.04.15  khubbard Creation
-- ***************************************************************************/
`default_nettype none // Strictly enforce all nets to be declared

module mesa_byte2ascii
(
  input  wire         clk,
  input  wire         reset,
  input  wire [7:0]   tx_byte_d,
  input  wire         tx_byte_en,
  output wire         tx_byte_busy,
  input  wire         tx_byte_done,
  output reg  [7:0]   tx_char_d,
  output wire         tx_char_en,
  input  wire         tx_char_busy,
  input  wire         tx_char_idle
);// module mesa_byte2ascii

  reg           tx_done_jk;
  reg           tx_done_char;
  reg           tx_en;
  reg           tx_en_p1;
  reg           tx_busy_jk;
  reg  [1:0]    tx_fsm;
  reg  [7:0]    tx_byte_sr;
  wire          tx_uart_busy;
  reg           tx_uart_busy_p1;
  reg  [7:0]    tx_uart_busy_sr;
  wire [3:0]    tx_nib_bin;

  `define ascii_lf       8'H0a
 
  assign tx_uart_busy = tx_char_busy;
  assign tx_char_en   = tx_en;
  assign tx_byte_busy = tx_busy_jk | tx_byte_en;

//-----------------------------------------------------------------------------
// When a binary byte is sent to transmit, immediately assert tx_busy
// then convert 1of2 nibbles to ASCII and send out until byte is done.
// If tx_done asserts, wait for tx_busy to drop and then send a "\n".
// 0x30 = "0"
// 0x39 = "9"
// 0x41 = "A"
// 0x46 = "F"
// 0x61 = "a"
// 0x66 = "f"
//-----------------------------------------------------------------------------
always @ ( posedge clk ) begin : proc_tx  
  tx_done_char    <= 0;
  tx_en           <= 0;
  tx_en_p1        <= tx_en;
  tx_uart_busy_p1 <= tx_uart_busy;
  tx_uart_busy_sr <= { tx_uart_busy_sr[6:0], tx_uart_busy };

  // Remember that the transmission was complete to queue up a "\n"
  if ( tx_byte_done == 1 ) begin
    tx_done_jk <= 1;
  end

  // Deassert tx_busy_jk on falling edge of UART busy when no more nibbles
//if ( tx_fsm == 2'b00 && tx_uart_busy_p1 == 1 && tx_uart_busy == 0 ) begin
  if ( tx_fsm == 2'b00 && 
       tx_char_idle == 1   ) begin
//     tx_uart_busy_sr[7:6] == 2'b10 && 
//     tx_uart_busy == 0                 ) begin
    tx_busy_jk <= 0;
    if ( tx_done_jk == 1 ) begin
      tx_done_char <= 1;
      tx_done_jk   <= 0;
    end
  end 

  // Queue up a binary byte to convert to ASCII as 2 nibbles for UART 
  if ( tx_byte_en == 1 ) begin
    tx_busy_jk <= 1;
    tx_fsm     <= 2'b11;
    tx_byte_sr <= tx_byte_d[7:0];
  end

  // Shift out a nibble when ready
  if ( tx_fsm != 2'b00 && tx_uart_busy == 0 && 
       tx_en == 0 && tx_en_p1 == 0 ) begin
    tx_en       <= 1;// Send tx_char_d[7:0] to UART 
    tx_fsm[1:0] <= { tx_fsm[0], 1'b0 };
    tx_byte_sr  <= { tx_byte_sr[3:0], 4'd0 };// Nibble Shift
  end

  // tx_char_d[7:0] is ASCII converted version of tx_nib_bin[3:0]
  if ( tx_nib_bin < 4'hA ) begin
    tx_char_d[7:0] <= 8'h30 + tx_nib_bin[3:0];// 0x30-0x00=0x30 (duh)
  end else begin
    tx_char_d[7:0] <= 8'h37 + tx_nib_bin[3:0];// 0x41-0x0A=0x37
  end

  // Was a "\n" queued up? Send it now
  if ( tx_done_char == 1 ) begin
    tx_char_d[7:0] <= `ascii_lf; // aka "\n"
    tx_en          <= 1;
  end

  if ( reset == 1 ) begin
    tx_busy_jk <= 0;
    tx_done_jk <= 0;
    tx_fsm     <= 2'b00;
  end 
end
  assign tx_nib_bin = tx_byte_sr[7:4];


endmodule // mesa_byte2ascii
