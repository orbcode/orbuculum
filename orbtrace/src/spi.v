`default_nettype none

module spi (
	    input 	 clk, // The master clock for this module
	    input 	 rst, // Synchronous reset.

	    output reg 	 tx, // Outgoing serial line
	    input        rx,
 	 
	    input 	 dClk,
	    input 	 cs, 
	    input 	 transmitIn, // Signal to transmit
	    input [15:0] tx_word, // Byte to transmit
	    output 	 tx_free, // Indicator that transmit register is available
	    output 	 is_transmitting, // Low when transmit line is idle.
	    input 	 sync,
	    input [2:0]  widthEnc,
	    output 	 rxFrameReset
	    );

   reg 			 realTransmission;    // Is this real data or an empty frame
   reg [1:0] 		 width;               // How wide the pins are on the CPU (0-->1, 1-->2, 2-->x, 3-->4)
 			 
   reg [15:0] 		 tx_ledstretch;       // Stretch the LED so it can be seen
   reg [4:0] 		 tx_bits_remaining;   // How many bits in this word of the frame left?
   reg [3:0] 		 tx_words_remaining;  // How many words in this frame left
   reg [15:0] 		 tx_data;             // Holding for the data being shifted to line
   reg [7:0] 		 rx_data;             // Holding for the data being received from line
   reg [2:0] 		 bitcount;            // Number of bits of this byte rxed (or txed) so far
		 
   // Light the LED if we are transmitting (or have been recently)
   assign is_transmitting=(tx_ledstretch!=0);
   

   always @(posedge clk)    // Check transmission state and store it (done using main clock) ========
     if (rst) tx_ledstretch <= 0;
     else
       begin
	  if (realTransmission && cs==0) tx_ledstretch<=~0;
	  else
	    if (tx_ledstretch!=0) tx_ledstretch <= tx_ledstretch-1;
       end


   always @(negedge dClk)  // Capture the bit from the SPI master ===================================
     begin
	if (rst) bitcount=0;
	else
	  begin
	     rx_data={rx_data[6:0],rx};
	     
	     // Re-sync the frame if we need to
	     if (rx_data==8'hA5)  bitcount=0;
	     else
	       bitcount=bitcount+1;
	  end
     end // always @ (negedge dClk)

   
   always @(posedge dClk)  // Send output bits clocked by SPI clk ===================================

     begin
	if (rst)
	  begin
	     width<=3;
	     widthEnc<=4;
	  end
	else
	  begin
	   if ((bitcount==0) && (rx_data!=0))
	     begin
		// Handle received commands
		if (rx_data==8'hA5)
		  begin
		     // Reset command - roll back any part completed frame
		     rxFrameReset<=1;
		  end
		else
		  rxFrameReset<=0;
		
		if ({rx_data[7:4],3'b0,rx_data[0]}==8'hA0)
		  begin
		     // Only really interested in the one that sets up the transmission
		     width<=rx_data[3:2];
		     widthEnc<={1'b0,rx_data[3:2]}+1;

		     // We need these right now to be able to make sense of the following...
		     tx_words_remaining = 8;
		     realTransmission = transmitIn;
		     tx_data = {!transmitIn,4'h0,width,sync,8'h00};
		     tx_bits_remaining=7;
		  end // if ({rx_data[7:4],3'b0,rx_data[0]}==8'hA0)
	     end // if ((bitcount==0) && (rx_data!=0))
	     
	     // Deal with transmission process
	     tx=tx_data[15];
	     if (tx_bits_remaining==0)
	       begin
		  // End of this word, find the next one
		  if (tx_words_remaining==0)
		    begin
		       tx_words_remaining <= 8;
		       realTransmission <= transmitIn;
		       tx_data <= {!transmitIn,4'h0,width,sync,8'h00};
		       tx_bits_remaining<=7;
		    end
		  else
		    begin
		       // We have more words in this frame - get them
		       if (realTransmission)
			 begin
			    // We send these to the host the other way up
			    tx_data<={tx_word[7:0],tx_word[15:8]};

			    // ...and get the next word ready
			    tx_free<=1;
			 end
		       else
			 begin
			    // If this isn't a real data frame then transmit zeros
			    tx_data<=0;
			 end
		       
		       // Whatever happened, we have a frame to transmit now
		       tx_bits_remaining<=15;
		       tx_words_remaining<=tx_words_remaining-1;
		    end // else: !if(tx_words_remaining==0)
	       end // if (tx_bits_remaining==0)
	     else
	       begin
		  // Reset next data request if it's stretched for long enough
		  tx_free<=0;
		  tx_bits_remaining<=tx_bits_remaining-1;
		  tx_data<={tx_data[14:0],1'b0};
	       end // else: !if(tx_bits_remaining==0)
	  end // else: !if(rst)
     end // always @ (posedge dClk)
endmodule // spi


