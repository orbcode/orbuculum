`default_nettype none

module spi (
	    input              rst,
            input              clk,

	    output             Tx,            // Outgoing serial line
	    input              Rx,
            input              Cs,
	    input              DClk,          // Clock for transmit

	    input [127:0]      Tx_packet,     // Packet to transmit
            output reg         TxGetNext,     // Toggle to request next packet

            output reg         PktComplete,   // Toggle indicating data received 
            output reg [31:0]  RxedFrame      // The frame of data received
	    );

   reg [7:0] 		 bitcount;            // How many bits in this frame left?

   reg [127:0] 		 tx_data;             // Holding for the data being shifted to line
   reg [31:0] 		 rx_data;             // Holding for the data being received from line
   reg                   isRx;                // Flag set for Rx (initial part of frame)

   reg                   inhibited;           // Indicator for lockout due to CS high
   reg                   inhibited_clr;       // Clear of inhibited indicator

   
   /* CPOL=0, CPHA=0: Read on rising edge, write on falling edge. */

   assign Tx = (Cs || (isRx && (bitcount>1)))?1'b1:tx_data[127];
             
   always @(posedge DClk,posedge inhibited)
     begin
        if (inhibited)
          begin
             inhibited_clr<=1'b1;                // The inhibit flag can clear now
             isRx<=1'b1;                         // ...and we do that read before writes
          end
        else
          if (isRx)
            begin
               rx_data<={rx_data[30:0],Rx};
             
               if (bitcount==0)
                 begin
                    isRx<=1'b0;
                    RxedFrame<=rx_data;         // Finished, store the data
                    PktComplete<=!PktComplete;  // ..and tell anyone who needs to know
                 end
            end // if (isRx)
     end // always @ (posedge DClk)
      
   always @(negedge DClk, posedge inhibited)
     begin
     if (inhibited)
       begin
          bitcount<=8'd32;                    // Next read will be 32 bits
       end
     else
       begin
          tx_data<={tx_data[126:0],1'b0};     // In the absence of other, move tx along

          /* Toggle to get next data ready */
          if (bitcount==96) TxGetNext<=!TxGetNext;

          /* When we reach zero we will need to start shifting tx data in */
          /* this is done early because of the pipelining effects         */
          if (bitcount==1) tx_data<=Tx_packet;
          
          /* Now adjust number of bits left */
          if (bitcount==0) bitcount<=8'h7f;
          else
            bitcount<=bitcount-1;
       end // else: !if(inhibited)
     end // always @ (negedge DClk, posedge inhibited)
    
   always @(*) // Monitor CS
     begin
        if (Cs) inhibited<=1'b1;
        else
          if (inhibited_clr==1'b1) inhibited<=1'b0;
     end
endmodule // spi


