`default_nettype none

// Documented Verilog UART
//
// Based on work that was Copyright (C) 2010 Timothy Goddard (tim@goddard.net.nz)
// Distributed under the MIT licence.
// Extensively modified thereafter by Dave Marples (dave@marples.net)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 

module uart(
	    input        clk,
	    input        rst,

    // Hardware Interface
	    input        rx,              // Incoming serial line
	    output reg   tx,              // Outgoing serial line

    // Transmit Side
	    input        transmit,        // Signal to transmit
	    input [7:0]  tx_byte,         // Byte to transmit
	    output       tx_free,         // Indicator that transmit register empty
	    output       is_transmitting, // Low when transmit line is idle.

    // Receive Side
	    output       received,        // Indicated that a byte has been received.
	    output [7:0] rx_byte,         // Byte received
	    output       is_receiving,    // Low when receive line is idle.
	    output       recv_error       // Indicates error in receiving packet.
	    );
   
   parameter CLOCKFRQ=48_000_000;         // Frequency of the oscillator
   parameter BAUDRATE=12_000_000;         // Required baudrate

   parameter TICKS_PER_BIT = (CLOCKFRQ/BAUDRATE)-1;
   
   // States for the receiving state machine.
   // These are just constants, not parameters to override.
   parameter RX_IDLE          = 0;
   parameter RX_CHECK_START   = 1;
   parameter RX_READ_BITS     = 2;
   parameter RX_CHECK_STOP    = 3;
   parameter RX_DELAY_RESTART = 4;
   parameter RX_ERROR         = 5;
   parameter RX_RECEIVED      = 6;
   
   // States for the transmitting state machine.
   // Constants - do not override.
   parameter TX_IDLE          = 0;
   parameter TX_SENDING       = 1;
   
   reg [2:0] 		 rx_state;
   reg [12:0] 		 rx_countdown;
   reg [3:0] 		 rx_bits_remaining;
   reg [7:0] 		 rx_data;
   
   reg  		 tx_state;
   reg [12:0] 		 tx_countdown;
   reg [3:0] 		 tx_bits_remaining;
   reg [7:0] 		 tx_data;
   
   reg [16:0] 		 tx_ledstretch;
   reg [16:0] 		 rx_ledstretch;
   
   assign is_receiving = (rx_ledstretch != 0); 
   assign is_transmitting = (tx_ledstretch != 0);
   assign rx_byte = rx_data;
   
   assign received   = (rx_state == RX_RECEIVED);
   assign recv_error = (rx_state == RX_ERROR);
   assign tx_free    = (tx_state == TX_IDLE);
   
   always @(posedge clk,posedge rst) begin
      if (rst) begin
	 rx_state       <= RX_IDLE;
	 tx_state       <= TX_IDLE;
         tx_countdown   <= 0;
	 rx_ledstretch  <= 0;
	 tx_ledstretch  <= 0;
	 tx             <= 1;
      end
      else
	begin

	   if (rx_ledstretch!=0) rx_ledstretch <= rx_ledstretch-1;
	   if (tx_ledstretch!=0) tx_ledstretch <= tx_ledstretch-1;
           if (rx_countdown!=0) rx_countdown<=rx_countdown-1;
           if (tx_countdown!=0) tx_countdown<=tx_countdown-1;

	   // Receive state machine
	   case (rx_state)
	     RX_IDLE: // -------------------------------------------------------------
	       begin
		  // A low pulse on the receive line indicates the
		  // start of data.
		  if (!rx) 
		    begin
		       // Wait half the period - should resume in the
		       // middle of this first pulse.
		       rx_countdown <= TICKS_PER_BIT/2;
		       rx_state <= RX_CHECK_START;
		    end
	       end // case: RX_IDLE

	     RX_CHECK_START: // ------------------------------------------------------
	       // Check the start pulse is still there
	       if (rx_countdown==0)
		 begin
		    if (!rx)
		      begin
			 // Wait the bit period to resume half-way
			 // through the first bit.
			 rx_countdown <= TICKS_PER_BIT;
			 rx_bits_remaining <= 7;
			 rx_ledstretch <= ~0;
			 rx_state <= RX_READ_BITS;
		      end
		    else
		      // Pulse lasted less than half the period -
		      // not a valid transmission.
                      rx_state <= RX_ERROR;
		 end // if (rx_countdown==0)

	     RX_READ_BITS: // -------------------------------------------------------
	       // Should be half-way through a bit pulse here.
	       // Read this bit in, wait for the next if we
	       // have more to get.
	       if (rx_countdown==0)
		 begin
		    rx_data <= {rx, rx_data[7:1]};
		    rx_countdown <= TICKS_PER_BIT;
		    rx_bits_remaining <= rx_bits_remaining - 1;
		    rx_state <= (rx_bits_remaining!=0) ? RX_READ_BITS : RX_CHECK_STOP;
		 end

	     RX_CHECK_STOP:  // -----------------------------------------------------
	       // Should resume half-way through the stop bit
	       // This should be high - if not, reject the
	       // transmission and signal an error.
	       if (rx_countdown==0)
                 begin
                    rx_countdown<=TICKS_PER_BIT;
                    rx_state=rx?RX_RECEIVED:RX_ERROR;
                 end

	     RX_DELAY_RESTART: // --------------------------------------------------
	       // Waits a set number of cycles before accepting
	       // another transmission.
               if (rx_countdown==0) rx_state <=  RX_IDLE;

	     RX_ERROR: // ----------------------------------------------------------
	       // There was an error receiving.
	       // Raises the recv_error flag for one clock
	       // cycle while in this state and then waits
	       // 2 bit periods before accepting another
	       // transmission.
               begin
		  rx_countdown <= TICKS_PER_BIT;
		  rx_state <= RX_DELAY_RESTART;
	       end

	     RX_RECEIVED: // -------------------------------------------------------
	       // Successfully received a byte.
	       // Raises the received flag for one clock
	       // cycle while in this state.
	       rx_state <= RX_IDLE;

             default: // -----------------------------------------------------------
               rx_state <= RX_ERROR;
	   endcase
	   
	   // Transmit state machine
	   case (tx_state)
	     TX_IDLE: // -----------------------------------------------------------
	       begin
		  if (transmit) 
		    begin
		       // If the transmit flag is raised in the idle
		       // state, start transmitting the current content
		       // of the tx_byte input.
		       tx_ledstretch <= ~0;
		       
		       tx_data <= tx_byte;
		       // Send the initial, low pulse of 1 bit period
		       // to signal the start, followed by the data

		       tx_countdown <= TICKS_PER_BIT;
		       tx <= 0;
		       tx_bits_remaining <= 9; // This includes the stopbit
		       tx_state <= TX_SENDING;
		    end // if (transmit)
		  else
		    begin
		       tx <= 1;
		    end
	       end
	     
	     TX_SENDING: // --------------------------------------------------------
               begin
                  if (tx_countdown==0)
                    begin
		       if (tx_bits_remaining!=0) 
			 begin
			    tx_bits_remaining <= tx_bits_remaining - 4'd1;
			    tx <= tx_data[0];
			    tx_data <= {1'b1, tx_data[7:1]}; // By shifting 1's we get stopbits automatically
			    tx_countdown <= TICKS_PER_BIT;
			    tx_state <= TX_SENDING;
			 end 
		       else 
			 begin
			    tx_state<=TX_IDLE;
			 end
		    end
              end
	   endcase // case (tx_state)
	end // else: !if(rst)
   end
endmodule // uart
