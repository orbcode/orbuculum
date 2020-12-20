`default_nettype none

// packBuild
// =========
//
module packBuild (
		input         clk, // System Clock
		input         rst, // Clock synchronised reset
		
	        // Downwards interface to packet processor : wrClk Clock
		input         wrClk, // Clock for write side operations to fifo
		input         WdAvail, // Flag indicating word is available
		input         PacketReset, // Flag indicating to start again
		input [15:0]  PacketWd, // The next packet word

		// Upwards interface to serial (or other) handler : clk Clock
		output [15:0] DataVal, // Output data value

		input         rdClk,
		input         DataNext, // Request for next data element
		output        DataReady, // Indicator that the next data element is available
		input         DataFrameReset, // Reset to start of data frame

                // Indicator that buffer overflowed
		output reg    DataOverf // Too much data in buffer
 		);

   // Internals ==============================================================================
   parameter BUFFLENLOG2=12;

   // DomB (System clk domain) registers
   reg [BUFFLENLOG2-1:0]	 outputRp;         // Read Element position in output buffer
   reg [BUFFLENLOG2-1:0] 	 outputRpPostBox;  // Postbox for output RP
   reg [BUFFLENLOG2-1:0] 	 outputRpDomA;     // Second clock domain copy of RP

   reg 				 tickA;
   reg [2:0]                     tickBPrime;   

   // DomA (Target clk domain) registers
   reg [BUFFLENLOG2-1:0]	 outputWp;         // Write Element position in output buffer
   reg [BUFFLENLOG2-4:0]	 outputWpPostBox;  // Postbox for output WP
   reg [BUFFLENLOG2-4:0] 	 outputWpDomB;     // Second clock domain copy of WP

   reg 				 tickB;
   reg [2:0]                     tickAPrime;
   
   reg 				 oldDataNext;
   
   ram #(BUFFLENLOG2,16) buff (
            .din(PacketWd),
            .write_en(WdAvail),
            .waddr(outputWp),
            .wclk(!wrClk),

            .raddr(outputRp),
            .rclk(rdClk),
            .dout(DataVal)
            );

   // ======= Write data to RAM using source domain clock =================================================

   always @(negedge wrClk,posedge rst)   // Clock DomA : From the target
     begin
        DataOverf<=0;     
	if (rst)
	  begin
	     outputWp<=0;
             outputRpDomA<=0;
             tickA=0;
	  end
	else
	  begin
             tickBPrime<={tickBPrime[1:0],tickB};
             if ((tickBPrime==3'b011) || (tickBPrime==3'b100))
	       outputRpDomA<=outputRpPostBox;
             
             if (WdAvail)               
               begin
                  // ..and if we've just completed a frame there are a few things to do                  
                  if (outputWp[2:0]==3'b111)
                    begin
                       $display("End of packet");                       
                       // ...check for overflow                       
                       if ((outputWp[BUFFLENLOG2-1:3]+1) == outputRpDomA)
                         begin
                            $display("Overflowed");
                            DataOverf<=1;
                            outputWp[2:0]<=3'b000;
                         end
                       else
                         begin
                            $display("Rolled on");
                            // Flag that the packet changed to the other domain
                            outputWp<=outputWp+1;
                            outputWpPostBox<=outputWp[BUFFLENLOG2-1:3]+1;
                            tickA<=!tickA;
                         end
                    end
                  else
                    outputWp<=outputWp+1;
             
               end // if (WdAvail)
             
             
	     if (PacketReset) 
	       begin
		  // Reset to start of this packet
		  outputWp<={outputWp[BUFFLENLOG2-1:3],3'b000};
	       end
	  end
     end
   
   
   // ======== Send data to upstairs if relevant, and dispatch to collect more from downstairs ============

   assign DataReady=(({outputWpDomB,3'b000}!=outputRp) && {outputWpDomB,3'b000}!=(outputRp+1));
   
   always @(posedge rdClk,posedge rst)  // Clock DomB : From the FPGA domain
     begin
	if (rst)
	  begin
	     // ============= Setup reset conditions ============================
	     outputRp<=0;
	     outputWpDomB<=0;
             tickB<=0;
	  end // else: !if(!rst)
	else
	  begin
             /* Transfer write pointer over into our DOMB (used for detecting if theres data present) */
             tickAPrime<={tickAPrime[1:0],tickA};
             if ((tickAPrime==3'b011) || (tickAPrime==3'b100))
	       outputWpDomB<=outputWpPostBox;

	     // Things to do if there's the potential to send a byte to the serial link
	     oldDataNext<=DataNext;

	     // Check if we need to roll back to start of this frame
	     if (DataFrameReset)
	          outputRp<={outputRp[BUFFLENLOG2-1:3],3'b000};                  
               
	     if ((oldDataNext==0) && (DataNext==1) && (DataReady))
               begin
                  outputRp<=outputRp+1;
                  /* Constantly transfer our read position into DOMA (used for detecting overflow) */
                  outputRpPostBox<=outputRp[BUFFLENLOG2-1:3];
                  tickB<=!tickB;                       
               end

	  end // else: !if(rst)
     end // always @ (posedge clk)
endmodule // packBuild
