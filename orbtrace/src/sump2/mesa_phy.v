/* ****************************************************************************
-- Source file: mesa_phy.v                
-- Date:        October 2015     
-- Author:      khubbard
-- Description: Interface the Byte binary stream of the internal Mesa-Bus with
--              the physical layer. For this case, using UARTs.
--              This instantiates the UARTs and takes care of the binary to
--              ASCII conversions for all directions.
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
-- Revision History:
-- Ver#  When      Who      What
-- ----  --------  -------- ---------------------------------------------------
-- 0.1   10.04.15  khubbard Creation
-- ***************************************************************************/
`default_nettype none // Strictly enforce all nets to be declared

module mesa_phy
(
  input  wire         clk,
  input  wire         reset,
  input  wire         disable_chain,
  input  wire         clr_baudlock,
  output wire         mesa_wi_baudlock,

  input  wire         mesa_wi,
  output wire         mesa_wo,
  input  wire         mesa_ri,
  output wire         mesa_ro,

  output wire         mesa_wi_nib_en,
  output wire [3:0]   mesa_wi_nib_d,
  input  wire         mesa_wo_byte_en,
  input  wire [7:0]   mesa_wo_byte_d,
  output wire         mesa_wo_busy,   
  input  wire         mesa_ro_byte_en,
  input  wire [7:0]   mesa_ro_byte_d,
  output wire         mesa_ro_busy,
  input  wire         mesa_ro_done   
);// module mesa_phy


  wire         wi_char_en;
  wire [7:0]   wi_char_d;
  wire         ro_char_en;
  wire [7:0]   ro_char_d;
  wire         ro_uart_busy;
  wire         ro_uart_idle;
  wire         wo_char_en;
  wire [7:0]   wo_char_d;
  wire         wo_uart_busy;
  wire         baud_lock;
  wire [15:0]  baud_rate;

  assign mesa_wi_baudlock = baud_lock;

//-----------------------------------------------------------------------------
// UART to convert serial streams to/from ASCII bytes for Wi and Ro.
//-----------------------------------------------------------------------------
mesa_uart u_mesa_uart
(
  .clk              ( clk             ),
  .reset            ( reset           ),
  .clr_baudlock     ( clr_baudlock    ),
  .en_autobaud      ( 1'b1            ),
//.en_autobaud      ( 1'b0            ),
  .rxd              ( mesa_wi         ),
  .txd              ( mesa_ro         ),
  .flush            (                 ),
  .dbg              (                 ),
  .rx_rdy           ( wi_char_en      ),
  .rx_byte          ( wi_char_d[7:0]  ),

  .tx_en            ( ro_char_en      ),
  .tx_byte          ( ro_char_d[7:0]  ),
  .tx_busy          ( ro_uart_busy    ),
  .tx_idle          ( ro_uart_idle    ),
  .rx_idle          (                 ),
  .baud_rate        ( baud_rate[15:0] ),
  .baud_lock        ( baud_lock       )
); // module mesa_uart


//-----------------------------------------------------------------------------
// TX Only UART. Sends Wo data. When 1st UART goes to lock, sends "\n" out, 
// otherwise just echos the binary stream from decode block ( Wi->Wo ).
//-----------------------------------------------------------------------------
mesa_tx_uart u_mesa_tx_uart
(
  .clk              ( clk                   ),
  .reset            ( reset | disable_chain ),
  .txd              ( mesa_wo               ),
  .tx_en            ( wo_char_en            ),
  .tx_byte          ( wo_char_d[7:0]        ),
  .tx_busy          ( wo_uart_busy          ),
  .baud_rate        ( baud_rate[15:0]       ),
  .baud_lock        ( baud_lock             )
); // module mesa_uart


//-----------------------------------------------------------------------------
// Convert Wi ASCII to Binary Nibbles. Decoder figures out nibble/byte phase
//-----------------------------------------------------------------------------
mesa_ascii2nibble u_mesa_ascii2nibble
(
  .clk              ( clk                ),
  .rx_char_en       ( wi_char_en         ),
  .rx_char_d        ( wi_char_d[7:0]     ),
  .rx_nib_en        ( mesa_wi_nib_en     ),
  .rx_nib_d         ( mesa_wi_nib_d[3:0] )
);// module mesa_ascii2nibble


//-----------------------------------------------------------------------------
// Convert Ro Binary Bytes to ASCII 
//-----------------------------------------------------------------------------
mesa_byte2ascii u0_mesa_byte2ascii
(
  .clk              ( clk                 ),
  .reset            ( reset               ),
  .tx_byte_en       ( mesa_ro_byte_en     ),
  .tx_byte_d        ( mesa_ro_byte_d[7:0] ),
  .tx_byte_busy     ( mesa_ro_busy        ),
  .tx_byte_done     ( mesa_ro_done        ),
  .tx_char_en       ( ro_char_en          ),
  .tx_char_d        ( ro_char_d[7:0]      ),
  .tx_char_busy     ( ro_uart_busy        ),
  .tx_char_idle     ( ro_uart_idle        ) 
);// module mesa_byte2ascii


//-----------------------------------------------------------------------------
// Convert Wo Binary Bytes to ASCII 
//-----------------------------------------------------------------------------
mesa_byte2ascii u1_mesa_byte2ascii
(
  .clk              ( clk                   ),
  .reset            ( reset | disable_chain ),
  .tx_byte_en       ( mesa_wo_byte_en       ),
  .tx_byte_d        ( mesa_wo_byte_d[7:0]   ),
  .tx_byte_busy     ( mesa_wo_busy          ),
  .tx_byte_done     ( 1'b0                  ),
  .tx_char_en       ( wo_char_en            ),
  .tx_char_d        ( wo_char_d[7:0]        ),
  .tx_char_busy     ( wo_uart_busy          ) 
);// module mesa_byte2ascii


endmodule // mesa_phy.v
