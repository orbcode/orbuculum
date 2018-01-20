`default_nettype none

// traceIF
// =======
// Concatenates defined bus width input data from TRACEDATA using TRACECLK to bring it into
// 16 bit chunks which are stored in memory if they're not 'pass' cases. Also deals with top
// level sync. Above this level nothing needs to know how wide the tracebus actually is.
// 
// Working from CoreSight TPIU-Lite Technical Reference Manual Revision: r0p0

module traceIF # (parameter BUSWIDTH = 4) (
		input 		     clk,         // System Clock
		input 		     rst,         // Reset synchronised to clock

	// Downwards interface to the trace pins
		input [BUSWIDTH-1:0] traceDina,   // Tracedata rising edge... 1-n bits max, can be less
		input [BUSWIDTH-1:0] traceDinb,   // Tracedata falling edge... 1-n bits max, can be less
		input 		     traceClkin,  // Tracedata clock... async to clk
		input [2:0] 	     width,       // How wide the bus under consideration is 1..4 (0, 3 & >4 invalid)

	// Upwards interface to packet processor
		output reg 	     WdAvail,     // Flag indicating word is available
		output [15:0]        PacketWd,    // The next packet word
		output reg 	     PacketReset, // Flag indicating to start again

		output 		     sync         // Indicator that we are in sync
		);		  
   
   // Internals =============================================================================

   // Frame maintenance
   reg [5:0] 	ofs;                      // Offset of useful data in the recieved frame
   reg [35:0] 	construct;                // Track of data being constructed (extra bits for mis-sync)
   reg [4:0] 	readBits;                 // How many bits of the sample we have

   // Sync checking stuff
   reg [2:0] 	gotSync;                  // Pulse stretch between domains for sync
   reg          prevSync;                 // Previous sync state used for detecting edge
   reg [21:0] 	lostSync;                 // Counter for sync loss timeout

   // ================== Input data against traceclock from target system ====================
   always @(posedge traceClkin)
     begin
	if (rst)
	  begin
	     construct<=0;
	     gotSync<=0;
	     WdAvail<=0;
	     PacketReset<=0;
	  end
	else
	  begin
	     // Deal with sync flagging ===============================================

	     // Check if signal starts on even or odd side of the DDR 
	     if (!sync)
	       begin
		  if (construct[35 -: 32]==32'h7fff_ffff) ofs<=35;
		  else
		  case (width)
		    1: if (construct[34 -: 32]==32'h7fff_ffff) ofs<=34;
		    2: if (construct[33 -: 32]==32'h7fff_ffff) ofs<=33;
		    4: if (construct[31 -: 32]==32'h7fff_ffff) ofs<=31;
		  endcase // case (width)
	       end // if (!sync)
	     
	     PacketWd<=construct[ofs -:16];

	     if (construct[ofs -:32]==32'h7fff_ffff)
	       begin
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
		  if (readBits[4]==1'b1)
		    begin
		       // Either a 7fff packet to be ignored or real data. Either way, we got 16 bits
		       // of it, so reset the bits counter (bearing in mind we'll take a sample this cycle)
		       readBits<={2'b0,width}<<1;
		       if ((gotSync!=0) || sync)
			 begin
			    // Got a full set of bits, so store them ... except if they're useless
			    if (construct[ofs -:16]==16'h7fff)
			      WdAvail<=1'b0;
			    else
			      WdAvail<=1'b1;
			 end // if ((gotSync!=0) || sync)
		    end // if (readBits==16)
		  else
		    begin
		       WdAvail<=1'b0;
		       // We are still collecting data...increment the bits counter
		       readBits<=readBits+({2'b0,width}<<1);
		    end // else: !if(readBits[4]==1'b1)
	       end // else: !if(construct[ofs -:32]==32'h7fff_ffff)

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
	  end // else: !if(!rst)
	else
	  begin	     
	     // Let everyone know the state of sync at the moment
	     sync<=(lostSync!=0);

	     // ...and count down sync in case we don't see it again
	     prevSync<=(gotSync==0);
	     if ((gotSync!=0) && (prevSync==1'b1)) lostSync<=~0;
	     else 
	       if (lostSync>0) lostSync<=lostSync-1;
	  end // else: !if(rst)
     end // always @ ( posedge clk )
endmodule // traceIF
