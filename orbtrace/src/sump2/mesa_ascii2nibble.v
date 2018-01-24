/* ****************************************************************************
-- Source file: mesa_ascii2nibble.v                
-- Date:        October 4, 2015
-- Author:      khubbard
-- Description: Convert an ASCII character to a binary nibble.
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

module mesa_ascii2nibble
(
  input  wire         clk,
  input  wire         rx_char_en,
  input  wire [7:0]   rx_char_d,
  output reg          rx_nib_en,
  output reg  [3:0]   rx_nib_d
);// module mesa_ascii2nibble

  reg  [4:0]   rx_nib_bin; // MSB is Valid Char Flag for "0"-"F"


//-----------------------------------------------------------------------------
// When UART receives an ASCII char, see if it is 0-9,a-f,A-F else toss   
//-----------------------------------------------------------------------------
always @ ( posedge clk ) begin : proc_rx  
  rx_nib_d   <= rx_nib_bin[3:0];
  rx_nib_en  <= rx_char_en & rx_nib_bin[4];
end


//-----------------------------------------------------------------------------
// Convert ASCII to binary nibbles. Toss and ignore all other received chars
// 0x30 = "0"
// 0x39 = "9"
// 0x41 = "A"
// 0x46 = "F"
// 0x61 = "a"
// 0x66 = "f"
//-----------------------------------------------------------------------------
always @ ( * ) begin : proc_lut
 begin
  case( rx_char_d[7:0] )
    8'h30   : rx_nib_bin[4:0] <= 5'h10;// 0
    8'h31   : rx_nib_bin[4:0] <= 5'h11;
    8'h32   : rx_nib_bin[4:0] <= 5'h12;
    8'h33   : rx_nib_bin[4:0] <= 5'h13;
    8'h34   : rx_nib_bin[4:0] <= 5'h14;
    8'h35   : rx_nib_bin[4:0] <= 5'h15;
    8'h36   : rx_nib_bin[4:0] <= 5'h16;
    8'h37   : rx_nib_bin[4:0] <= 5'h17;
    8'h38   : rx_nib_bin[4:0] <= 5'h18;
    8'h39   : rx_nib_bin[4:0] <= 5'h19;// 9

    8'h41   : rx_nib_bin[4:0] <= 5'h1A;// A
    8'h42   : rx_nib_bin[4:0] <= 5'h1B;
    8'h43   : rx_nib_bin[4:0] <= 5'h1C;
    8'h44   : rx_nib_bin[4:0] <= 5'h1D;
    8'h45   : rx_nib_bin[4:0] <= 5'h1E;
    8'h46   : rx_nib_bin[4:0] <= 5'h1F;// F

    8'h61   : rx_nib_bin[4:0] <= 5'h1A;// a
    8'h62   : rx_nib_bin[4:0] <= 5'h1B;
    8'h63   : rx_nib_bin[4:0] <= 5'h1C;
    8'h64   : rx_nib_bin[4:0] <= 5'h1D;
    8'h65   : rx_nib_bin[4:0] <= 5'h1E;
    8'h66   : rx_nib_bin[4:0] <= 5'h1F;// f

    default : rx_nib_bin[4:0] <= 5'h0F;
  endcase
 end
end // proc_lut


endmodule // mesa_ascii2nibble
