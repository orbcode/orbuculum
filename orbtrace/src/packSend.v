`default_nettype none

// packSend
// ========
//
module packSend (
		input 	     clk, // System Clock
		input 	     rst, // Clock synchronised reset
		
		input 	     sync, // Indicator of if we are in sync
		
	        // Downwards interface to packet processor
		input 	     wrClk, // Clock for write side operations to fifo
		input 	     WdAvail, // Flag indicating word is available
		input 	     PacketReset, // Flag indicating to start again
		input [15:0] PacketWd, // The next packet word
		input 	     PacketCommit, // Flag indicating packet is complete and can be processed
		
		// Upwards interface to serial (or other) handler
		output [7:0] DataVal, // Output data value
		input 	     DataNext, // Request for next data element
		output reg   DataReady,
		
		output 	     DataOverf // Too much data in buffer
 		);

   // Internals ==============================================================================
   parameter BUFFLENLOG2=10;

   reg [15:0] 			 opbuffmem[0:(2**BUFFLENLOG2)-1]; // Output buffer packet memory

   reg [BUFFLENLOG2-1:0]	 outputRp;         // Element position in output buffer
   reg [BUFFLENLOG2-1:0] 	 outputWpNext;     // Element position in output buffer for packet currently being written
   reg [BUFFLENLOG2-1:0] 	 opN;
   
   reg [BUFFLENLOG2-1:0]	 outputWp;         // Element position in output buffer

   reg 				 oldSync;          // Flag indicating previous sync state
 				
   reg [16:0] 			 ovfStretch;       // LED stretch for overflow indication

   reg [1:0] 			 queueState;       // State of the queuing mechanism
   reg 				 odd;              // Indicator of if even or add byte is being presented to uppoer level
   reg 				 PacketCommitSynced;
   reg [10:0] 			 syncPktInterval;
 			 
   reg [1:0] 			 syncOfs;
   
   
   parameter
     WR_FF12=0,
     WR_FF37=1,
     WR_NORMAL=2;

Signal_CrossDomain s (
    .SignalIn_clkA(PacketCommit),
    .clkB(clk),
    .SignalOut_clkB(PacketCommitSynced)
);
   
   // ======= Write data to RAM using source domain clock =================================================
   always @(posedge wrClk)
     if ((rst) || (sync && !oldSync))
       begin
	  queueState<=WR_FF12;
	  outputWpNext<=0;
	  ovfStretch<=0;
	  oldSync<=sync;
	  syncOfs<=0;
       end 
     else
       begin
	  oldSync<=sync;
	  if (ovfStretch!=0) ovfStretch<=ovfStretch-1;
	  if (PacketReset) outputWpNext<=outputWp+syncOfs;
	  else
	    begin
	       if (!PacketCommit)
		 begin
		    if (queueState!=WR_NORMAL)
		      begin
			 /* Write starter flag material */
			 queueState<=queueState+1;				
			 outputWpNext<=outputWpNext+1;
			 case (queueState)
			   WR_FF12:
			     begin
				opbuffmem[outputWpNext]<=16'hffff;
				syncOfs<=2'd1;
			     end
			   WR_FF37:
			     begin
				opbuffmem[outputWpNext]<=16'h7fff;
				syncOfs<=2'd2;
			     end
			 endcase
		      end
		    else
		      begin
			 /* Normal Queue State, See if there are any data to be filed */
			 if (WdAvail)
			   begin
			      opN=outputWpNext+1;
			      
			      if (opN!=outputRp)
				begin
				   opbuffmem[outputWpNext]<= PacketWd;
				   outputWpNext<=opN;
				end
			      else
				begin
				   /* Overflow condition - best flag it */
				   ovfStretch<=~0;
				end
			   end // if ((WdAvail) || (wordPending))
		      end // else: !if(queueState!=WR_NORMAL)
		 end // if (!PacketCommit)
	       else
		 begin
		    // No matter what, make sure we sync now and again
		    syncOfs<=0;
		    syncPktInterval<=syncPktInterval+1;
		    if (syncPktInterval==0) queueState<=WR_FF12;
		 end
		    
		    

	    end // else: !if(PacketReset)
       end // else: !if(rst)
 
   
   // ======== Send data to upstairs if relevant, and dispatch to collect more from downstairs ============

   assign DataOverf=(ovfStretch>0);
   
   always @(posedge clk)
     begin
	if (rst)
	  begin
	     // ============= Setup reset conditions ============================
	     outputRp<=0;
	     outputWp<=0;
	     odd<=0;
	     DataReady<=0;
	  end // else: !if(!rst)
	else
	  begin
	     if (PacketCommitSynced) outputWp<=outputWpNext;
	     
	     DataVal<=(odd)?opbuffmem[outputRp][15:8]:opbuffmem[outputRp][7:0];
	     	     
	     // Check to see if anyone is asking us for data
	     if (DataNext) 
	       begin
		  if ((outputWp!=outputRp) && (!DataReady)) 
		    begin
		       DataReady<=1;
		       if (odd)
			 begin
			    odd <= 0;
			    outputRp<=outputRp+1;
			 end
		       else
			 odd <= 1'b1;
		       
		    end
		  else
		    DataReady<=0;
	       end
	  end // if (!rst)
     end // always @ (posedge clk)
endmodule // packSend
