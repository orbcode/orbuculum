`default_nettype none

// packSend
// ========
//
module packSend (
		input 	     clk, // System Clock
		input 	     rst, // Clock synchronised reset
		
		input 	     sync, // Indicator of if we are in sync
		
	        // Downwards interface to packet processor : wrClk Clock
		input 	     wrClk, // Clock for write side operations to fifo
		input 	     WdAvail, // Flag indicating word is available
		input 	     PacketReset, // Flag indicating to start again
		input [15:0] PacketWd, // The next packet word

		// Upwards interface to serial (or other) handler : clk Clock
		output [7:0] DataVal, // Output data value
		input 	     DataNext, // Request for next data element
		output reg   DataReady,
		
		output 	     DataOverf // Too much data in buffer
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
   
   reg [16:0]                    syncArm;          // Interval countdown for sending sync pulses for keepalive
   reg [1:0] 			 syncSending;      // Current state of sync sending process

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
   
   always @(posedge clk)  // Clock Domain B : From the system PLL
     begin
	if (rst)
	  begin
	     // ============= Setup reset conditions ============================
	     ovfStretch<=0;
	     outputRp<=0;
	     odd<=0;
	     DataReady<=0;
	     syncSending<=0;
	     syncArm<=0;
	     tickA2B<=0;
	     outputRpPostBox<=0;
	     lastTickA<=0;
	     outputWpDomB<=0;
	     tickB<=1;
	  end // else: !if(!rst)
	else
	  begin
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
	     
	     // Countdown interval to next sync transmission
	     if (syncArm!=0) syncArm<=syncArm-1;

	     // Things to do if there's the potential to send a byte to the serial link
	     if ((DataNext) & (!DataReady))
	       begin
		  if ((outputRp[2:0]!=0) || odd || syncArm!=0)
		    begin
		       // Check and send regular data element
		       if ({outputWpDomB,3'b000}!=outputRp)
			 begin
			    DataReady<=1;
			    odd<=!odd;
			    DataVal<=(odd)?opbuffmem[outputRp][15:8]:opbuffmem[outputRp][7:0];
			    if (odd) outputRp<=outputRp+1;
			 end // if (outputWpDomB!=outputRp)
		    end // if ((outputRp[2:0]!=0) || odd || syncArm!=0)
		  else
		    begin
		       // If we are at the start of a packet and the timeout has expired and we are
		       // sync'ed then re-generate a sync packet to the host
		       if ((syncArm==0) && (sync))
			 begin
			    case(syncSending)
			      0: DataVal<=8'hff;
			      1: DataVal<=8'hff;
			      2: DataVal<=8'hff;
			      3: 
				begin
				   DataVal<=8'h7f;
				   syncArm<=~0;
				end
			    endcase			 
			    syncSending<=syncSending+1;
			    DataReady<=1;
			 end
		    end // else: !if((outputRp[2:0]!=0) || odd || syncArm!=0)
	       end // if ((DataNext) & (!DataReady))
	     else
	       DataReady<=0;
	  end // else: !if(rst)
     end // always @ (posedge clk)
endmodule // packSend
