`default_nettype none

// traceIF
// =======
// Concatenates defined bus width input data from TRACEDATA using TRACECLK to bring it into
// 16 bit chunks which are stored in memory if they're not 'pass' cases. Also deals with top
// level sync. Above this level nothing needs to know how wide the tracebus actually is.
// 
// Working from CoreSight TPIU-Lite Technical Reference Manual Revision: r0p0

module traceIF # (parameter BUSWIDTH = 4) (
		input 		     clk, // System Clock
		input 		     rst, // Reset synchronised to clock

	// Downwards interface to the trace pins
		input [BUSWIDTH-1:0] traceDina, // Tracedata rising edge... 1-n bits max, can be less
		input [BUSWIDTH-1:0] traceDinb, // Tracedata falling edge... 1-n bits max, can be less	
		input 		     traceClkin, // Tracedata clock... async to clk
		input [2:0] 	     width, // How wide the bus under consideration is 1..4 (0, 3 & >4 invalid)

	// Upwards interface to packet processor
		output reg 	     WdAvail, // Flag indicating word is available
		output reg [15:0]    PacketWd, // The next packet word
		output reg 	     PacketReset, // Flag indicating to start again

		output 		     sync // Indicator that we are in sync
		);		  
   
   // Internals =============================================================================

   reg [35:0] 	construct;                // Track of data being constructed (extra bits for mis-sync)
   reg [4:0] 	readBits;                 // How many bits of the sample we have
   
   reg [1:0] 	gotSync;                  // Pulse stretch between domains for sync
   reg 		prevSync;
   
   
   reg [23:0] 	lostSync;                 // Counter for sync loss timeout

   reg [2:0] 	offset;                   // Offset in sync process
   reg [15:0] 	extract;
   
   wire 	newSync;                  // Indicator that sync has been discovered

   // ================== Input data against traceclock from target system ====================
   always @(posedge traceClkin)
     begin
	if (rst)
	  begin
	     readBits<=0;
	     construct<=0;
	     gotSync<=0;
	     WdAvail<=0;
	     PacketReset<=0;
	  end
	else
	  begin
	     newSync=1;
	     // Deal with sync flagging ===============================================
	     if (construct[35 -: 32]==32'h7fff_ffff) offset<=4;
	     else
	       if ((width==1) && (construct[34 -: 32]==32'h7fff_ffff)) offset<=3;
	       else
		 if ((width==2) && (construct[33 -: 32]==32'h7fff_ffff)) offset<=2;
		 else
		   if ((width==4) && (construct[31 -:32]==32'h7fff_ffff)) offset<=0;
		   else
		     newSync=0;
	     
	     if (newSync==1)
	       begin
		  construct<=0;
		  gotSync<=~0;                // We synced, let people know
		  readBits<={2'b0,width}<<1;  // ... and reset writing position in recevied data
		  PacketReset <= 1'b1;        // tell upstairs to erase any partial packet it got
		  WdAvail <= 0;
	       end
	     else
	       begin
		  if (gotSync!=0) gotSync<=gotSync-1;
		  PacketReset <= 1'b0; // Stop erasing any partial packets

		  // Deal with complete element reception ========================
		  if (((gotSync!=0) || sync) && (readBits>=16))
		    begin
		       // Either a 7fff packet to be ignored or real data. Either way, we got 16 bits
		       // of it, so reset the bits counter (bearing in mind we'll take a sample this cycle)
		       readBits<={2'b0,width}<<1;
		       
		       // Got a full set of bits, so store them ... except if they're useless
		       extract=construct[6'd31+{3'b0,offset} -:16];
		       
		       if (extract==16'h7fff)
			 WdAvail<=1'b0;
		       else
			 begin
			    PacketWd<=extract;
			//	      {5'b0,offset,construct[6'd31+{3'b0,offset} -:8]};
			//	      construct[6'd31+{3'b0,offset} -:16];
			    WdAvail<=1'b1;
			 end
		    end // if ((sync) && (readBits==16))
		  else
		    begin
		       WdAvail<=1'b0;
		       
		       // We are still collecting data...increment the bits counter
		       readBits<=readBits+({2'b0,width}<<1);
		    end // else: !if((sync) && (readBits==16))
	       end // else: !if(newSync==1)
	
	     // Now get the new data elements that have arrived
	     case (width)
	       1: construct<={traceDinb[0],traceDina[0],construct[35:2]};
	       2: construct<={traceDinb[1:0],traceDina[1:0],construct[35:4]};
	       4: construct<={traceDinb[3:0],traceDina[3:0],construct[35:8]};
	       default:  construct<=0;  // These cases cannot really occur!
	     endcase // case (width)
	  end // else: !if(rst)
     end // always @ (posedge traceClk)
   
   // ======================================================================
        
   always @( posedge clk ) 
     begin
	if (rst)
	  begin
	     lostSync<=0;
	     sync<=0;
	     prevSync<=0;
	  end // else: !if(!rst)
	else
	  begin	     
	     // ...and count down sync in case we don't see it again
	     prevSync<=(gotSync!=0);

	     // Let everyone know the state of sync at the moment
	     sync<=(lostSync!=0);
	     
	     if ((gotSync!=0) && (prevSync==0)) lostSync<=~0;
	     else 
	       if (lostSync>0) lostSync<=lostSync-1;
	  end // else: !if(rst)
     end // always @ ( posedge clk )
endmodule // traceIF
