// TPIU Signaling framework. 
// From CoreSight TPIU-Lite Technical Reference Manual Revision: r0p0 

`timescale 1ns/100ps

module traceIF_tb();
   parameter chunksize=(4-1);

   reg[3:0] traceDin;  // Port is always 4 bits wide, even if we use less
   reg 	    traceClk;
   reg 	    clk;
   reg 	    nRst;
   reg [1:0] width_tb;

   wire      dvalid_tb;
   wire [7:0] dout_tb;
   wire      sync_tb;
   
   integer  w;
   integer  simcount=0;
   
   traceIF DUT (
		.traceDin(traceDin),
		.width(width_tb),
		.traceClk(traceClk),
		.clk(clk),
		.nRst(nRst),

		.dvalid(dvalid_tb),
		.dOut(dout_tb),
		.sync(sync_tb)
		);
   
   //-----------------------------------------------------------
   task sendByte;
      input[7:0] byteToSend;
      integer bitsToSend;

      begin
	 bitsToSend=8;
	 while (bitsToSend>0) begin
	    traceDin[chunksize:0]=byteToSend;
	    #10;
	    traceClk<=!traceClk;
	    #10;
	    bitsToSend=bitsToSend-(chunksize+1);
	    byteToSend=byteToSend>>(chunksize+1);	    
	 end
      end
   endtask
   //-----------------------------------------------------------      
      

   always
     begin
	while (1)
	  begin
	     clk=~clk;
	     #2;
	  end
     end
      
   initial begin
      assign width_tb=chunksize;
      nRst=1;
      traceDin=0;
      traceClk=0;

      clk=0;
      #10;
      nRst=0;
      #10;
      nRst=1;
      #100;

      // Initially send some junk while out of sync
      sendByte(8'hfe);
      sendByte(8'h22);

      // Now send Sync Sequence
      sendByte(8'hff);
      sendByte(8'hff);
      sendByte(8'hff);
      sendByte(8'h7f);

      // ....and a sane message
      sendByte(8'h42);
      sendByte(8'h71);
      sendByte(8'h19);
      sendByte(8'h69);
      sendByte(8'h12);      
      #30;
      $finish;
      
   end
   initial begin
      $dumpfile("trace_IF.vcd");

      $dumpvars;
   end
endmodule // trace_IF_tb

   
   
