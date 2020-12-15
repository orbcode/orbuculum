`default_nettype none

// packBuild
// =========
//
module packBuild (
		input 	      clk, // System Clock
		input 	      rst, // Clock synchronised reset
		
		input 	      sync, // Indicator of if we are in sync
		
	        // Downwards interface to packet processor : wrClk Clock
		input 	      wrClk, // Clock for write side operations to fifo
		input 	      WdAvail, // Flag indicating word is available
		input 	      PacketReset, // Flag indicating to start again
		input [15:0]  PacketWd, // The next packet word

		// Upwards interface to serial (or other) handler : clk Clock
		output [15:0] DataVal, // Output data value

		input 	      rdClk,
		input 	      DataNext, // Request for next data element
		output reg    DataReady, // Indicator that the next data element is available
		input 	      DataFrameReset, // Reset to start of data frame
		output reg    FrameReady, // Indicator that a complete frame of data is available
 
		output 	      DataOverf // Too much data in buffer
 		);

   // Internals ==============================================================================
   parameter BUFFLENLOG2=12;

   reg [15:0] 			 opbuffmem[0:(2**BUFFLENLOG2)-1]; // Output buffer packet memory

   // DomB (System clk domain) registers
   reg [BUFFLENLOG2-1:0]	 outputRp;         // Read Element position in output buffer
   reg [BUFFLENLOG2-1:0] 	 outputRpPostBox;  // Postbox for output RP
   reg [BUFFLENLOG2-1:0] 	 outputRpDomA;     // Second clock domain copy of RP
   reg [1:0] 			 tickA2B;
   reg 				 tickA;
   reg 				 lastTickB;
   

   // DomA (Target clk domain) registers
   reg [BUFFLENLOG2-1:0]	 outputWp;         // Write Element position in output buffer
   reg [BUFFLENLOG2-4:0]	 outputWpPostBox;  // Postbox for output WP
   reg [BUFFLENLOG2-4:0] 	 outputWpDomB;     // Second clock domain copy of WP
   reg [1:0] 			 tickB2A;
   reg 				 tickB;
   reg 				 lastTickA;
   
   reg [BUFFLENLOG2-1:0] 	 opN;              // Output next value

   reg  			 ovfSet;           // Cross domain flag for ovfStretch
   reg [25:0] 			 ovfStretch;       // LED stretch for overflow indication

   reg 				 odd;              // Indicator of if even or add byte is being presented to upper level
   
   reg 				 oldDataNext;
   

   // ======= Write data to RAM using source domain clock =================================================
   always @(posedge wrClk) // Clock Domain A : From the target
     begin
	if (rst)
	  begin
	     outputWp<=0;
	     outputWpPostBox<=0;
	     outputRpDomA<=0;
	     ovfSet<=0;
	     tickB2A<=0;
	     lastTickB<=0;
	     tickA<=0;
	     oldDataNext<=0;
	  end
	else
	  begin
	     // Domain cross the sync tick from domain B (the system Clock domain)
	     tickB2A[0]<=tickB;
	     tickB2A[1]<=tickB2A[0];
	     lastTickB<=tickB2A[1];

	     // Get ReadPointer into our clock, so we can see if there is room for more data
	     if (lastTickB!=tickB2A[1])
	       begin
		  // Get the latest info about the read pointer
		  outputRpDomA<=outputRpPostBox;
	       end

	     if (PacketReset) 
	       begin
		  // Need to send a tick to get an update for Rp, but no need to re-send our postbox
		  outputWp<={outputWp[BUFFLENLOG2-1:3],3'b000};
		  tickA<=!tickA;
		  ovfSet<=1'b0;
	       end
	     else
	       begin
		  opN=outputWp+1;
		  
		  if (opN==outputRpDomA)
		    begin
		       /* Overflow condition - best flag it and flush buffers (when PacketReset Rxed) */
		       ovfSet<=1'b1;
		    end
		  else
		    begin
		       if (WdAvail)
			 begin
			    // Let the other end know something interesting happened if this will be a complete packet
			    if (opN[2:0]==0)
			      begin
				 outputWpPostBox<=outputWp[BUFFLENLOG2-1:3];
				 tickA<=!tickA;
			      end

			    if (!ovfSet)
			      begin
				 opbuffmem[outputWp]<=PacketWd;
				 outputWp<=opN;
			      end
			 end
		    end // else: !if(opN==outputRp)
	       end // else: !if(PacketReset)
	  end
     end
   
   
   // ======== Send data to upstairs if relevant, and dispatch to collect more from downstairs ============

   assign DataOverf=(ovfStretch>0);
   assign FrameReady=(outputWpDomB!=outputRp[BUFFLENLOG2-1:3]);
   assign DataReady=({outputWpDomB,3'b000}!=outputRp);
   
   always @(posedge rdClk)  // Clock Domain B : From the target domain
     begin
	if (rst)
	  begin
	     // ============= Setup reset conditions ============================
	     ovfStretch<=0;
	     outputRp<=0;
	     odd<=0;
	     DataReady<=0;
	     tickA2B<=0;
	     outputRpPostBox<=0;
	     lastTickA<=0;
	     outputWpDomB<=0;
	     tickB<=1;
	  end // else: !if(!rst)
	else
	  begin
	     DataVal<=opbuffmem[outputRp];
	     
	     // Domain cross the sync tick from domain B (the system Clock domain)
	     tickA2B[0]<=tickA;
	     tickA2B[1]<=tickA2B[0];
	     lastTickA<=tickA2B[1];

	     // Get WritePointer into our clock so we know what's available for transmission
	     // and post current value of Read Pointer.
	     if (lastTickA!=tickA2B[1])
	       begin
		  outputRpPostBox<=outputRp;
		  outputWpDomB<=outputWpPostBox;
		  tickB<=!tickB;
	       end

	     // If there's been an overflow then stretch it out so the pulse is visible
	     if (ovfSet!=0) ovfStretch<=~0;
	     else
	       if (ovfStretch!=0) ovfStretch<=ovfStretch-1;
	     
	     // Things to do if there's the potential to send a byte to the serial link
	     oldDataNext<=DataNext;

	     // Check if we need to roll back to start of this frame
	     if (DataFrameReset)
	       outputRp<={outputRp[BUFFLENLOG2-1:3],3'b000};
	     else
	       if ((oldDataNext==0) && (DataNext==1) && ({outputWpDomB,3'b000}!=outputRp)) outputRp<=outputRp+1;
	  end // else: !if(rst)
     end // always @ (posedge clk)
endmodule // packBuild
