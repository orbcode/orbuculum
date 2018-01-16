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
		input 	     PacketCommit, // Flag indicating packet is complete and can be processed
		
		// Upwards interface to serial (or other) handler : clk Clock
		output [9:0] DataVal, // Output data value
		input 	     DataNext, // Request for next data element
		output reg   DataReady,
		
		output 	     DataOverf // Too much data in buffer
 		);

   // Internals ==============================================================================
   parameter BUFFLENLOG2=12;

   reg [15:0] 			 opbuffmem[0:(2**BUFFLENLOG2)-1]; // Output buffer packet memory

   reg [BUFFLENLOG2-1:0]	 outputRp;         // Read Element position in output buffer
   reg [BUFFLENLOG2-1:0]	 outputRpGray;     // Read Element position in output buffer
   reg [BUFFLENLOG2-1:0] 	 outputRpGrayR;    // Element position in output buffer for packet currently being written
   reg [BUFFLENLOG2-1:0] 	 outputRpGrayRR;   // Element position in output buffer for packet currently being written

   reg [BUFFLENLOG2-1:0]	 outputWp;         // Write Element position in output buffer
   reg [BUFFLENLOG2-1:0]	 outputWpGrayR;    // Write Element position in output buffer
   reg [BUFFLENLOG2-1:0]	 outputWpGrayRR;   // Write Element position in output buffer

   reg [BUFFLENLOG2-1:0] 	 outputWpFrame;     // Element position in output buffer for packet currently being written
   reg [BUFFLENLOG2-1:0] 	 outputWpFrameGray; // Element position in output buffer for packet currently being written
   
   reg [BUFFLENLOG2-1:0] 	 opN;
   
   reg 				 ovfSet;           // Cross domain flag for ovfStretch
   reg [28:0] 			 ovfStretch;       // LED stretch for overflow indication

   reg 				 odd;              // Indicator of if even or add byte is being presented to upper level
   reg 				 holdoff;
   reg 				 bufferFlush;
   
   reg [20:0]                    syncArm;
   reg [1:0] 			 syncSending;


   // Gray Code Converter ====================================================================
   function [BUFFLENLOG2-1:0] toGray;
      input [BUFFLENLOG2-1:0] 	 inp;
      begin
	 toGray = {inp[BUFFLENLOG2-1],inp[BUFFLENLOG2-2:0] ^ inp[BUFFLENLOG2-1:1]};
      end
   endfunction // toGray

   // ======= Write data to RAM using source domain clock =================================================
   always @(posedge wrClk)
     begin
	if (rst)
	  begin
	     outputWpFrame<=0;
	     outputRpGrayR<=0;
	     outputRpGrayRR<=0;	     
	     outputWp<=0;
	     ovfSet<=0;
	     holdoff<=0;
	  end
	else
	  begin
	     // Output ReadPointer into our clock
	     outputRpGrayR<=outputRpGray;
	     outputRpGrayRR<=outputRpGrayR;	 

	     if (PacketCommit)
	       begin
		  holdoff<=0;
		  ovfSet<=0;
		  outputWp<=outputWpFrame;
	       end
	     else
	     if (PacketReset) 
	       begin
		  holdoff<=0;
		  ovfSet<=0;
		  outputWpFrame<=outputWp;
	       end
	     else
	       begin
		  opN=outputWpFrame+1;
		  
		  if (toGray(opN)==outputRpGray)
		    begin
		       /* Overflow condition - best flag it and flush buffers */
		       ovfSet<=1;
		       holdoff<=1;
		       outputWpFrame<=0;
		       outputWp<=0;
		    end
		  else
		    begin
		       ovfSet<=0;
		       bufferFlush<=0;
		       if ((WdAvail)  && (!holdoff))
			 begin
			    opbuffmem[outputWpFrame]<=PacketWd;
			    outputWpFrame<=opN;
			 end
		    end // else: !if(opN==outputRp)
	       end // else: !if(PacketReset)
	  end
     end
   
   
   // ======== Send data to upstairs if relevant, and dispatch to collect more from downstairs ============

   assign DataOverf=(ovfStretch>0);
   
   always @(posedge clk)
     begin
	if ((rst) || (ovfSet))
	  begin
	     // ============= Setup reset conditions ============================
	     outputRp<=0;
	     outputRpGray<=0;
	     outputWpGrayR<=0;
	     outputWpGrayRR<=0;
	     if (ovfSet)
	       ovfStretch<=~0;
	     else
	       ovfStretch<=0;
	     odd<=0;
	     DataReady<=0;
	     syncSending<=0;
	     syncArm<=0;
	  end // else: !if(!rst)
	else
	  begin
	     // Sync write position to our clock - only most significant bits
	     // because we should only write out complete, comitted, packets
	     outputWpGrayR<=toGray({outputWp[BUFFLENLOG2-1:3],3'b000});
	     outputWpGrayRR<=outputWpGrayR;	 

	     // Countdown interval to next sync transmission
	     if (syncArm!=0) syncArm<=syncArm-1;

	     // If there's been an overflow then stretch it out so the pulse is visible
	     if (ovfStretch!=0) ovfStretch<=ovfStretch-1;


	     // If we are at the end of a packet and the timeout has expired and we are
	     // sync'ed then re-generate a sync packet to the host
	     if ((outputRp[2:0]==0) && (!syncArm) && (!odd) && (sync))
	       begin
		  if ((DataNext) && (!DataReady))
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
		  else
		    DataReady<=0;
	       end // if ((outputRp==0) && (!syncArm))
	     else
	       begin
		  // Check to see if anyone is asking us for data ... if so, and we have any, dispatch it
		  if (DataNext) 
		    begin
		       if ((outputWpGrayRR!=outputRpGray) && (!DataReady))
			 begin
			    DataReady<=1;
			    odd<=!odd;
			    DataVal<=(odd)?opbuffmem[outputRp][15:8]:opbuffmem[outputRp][7:0];
			    if (odd) 
			      begin
				 outputRp<=outputRp+1;
				 outputRpGray<=toGray(outputRp+1);
			      end
			 end
		       else
			 DataReady<=0;
		    end // if (DataNext)
	       end
	  end // if (!rst)
     end // always @ (posedge clk)
endmodule // packSend
