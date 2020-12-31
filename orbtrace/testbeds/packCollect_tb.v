// TPIU Signaling framework. 
// From CoreSight TPIU-Lite Technical Reference Manual Revision: r0p0 

`timescale 1ns/100ps

// Tests for packetCollection (built on traceIF in combination with packBuild)
// Set width to 3, 2 and 1 to test each width
// Set PS to 0 and 1 to test phase differences
// Run with
//  iverilog -o r traceIF.v packBuild.v ram.v ../testbeds/packCollect_tb.v  ; vvp r

module packCollect_tb;
   parameter WIDTH=4;    // 3=4 bit, 2=2 bit, 1,0=1 bit
   parameter PS=1;       // If to implement phase shift

   parameter chunksize=(WIDTH==3)?3:(WIDTH==2)?1:0;

   // Testbed interface 
   reg[3:0] traceDinA_tb;
   reg[3:0] traceDinB_tb;
   reg [1:0] width_tb;
   
   reg 	    traceClk_tb;
   reg 	    clk_tb;
   reg 	    rst_tb;

   wire        PkAvail_tb;
   wire [127:0] dout_tb;

   wire        sync_tb;

traceIF traceIF_DUT (
		.rst(rst_tb),                  // Reset synchronised to clock

	// Downwards interface to the trace pins
		.traceDina(traceDinA_tb),      // Tracedata rising edge... 1-n bits max, can be less
		.traceDinb(traceDinB_tb),      // Tracedata falling edge... 1-n bits max, can be less
		.traceClkin(traceClk_tb),      // Tracedata clock... async to clk
		.width(width_tb),              // How wide the bus under consideration is 0..3 (1, 1, 2 & 4 bits)

	// Upwards interface to packet processor
		.PkAvail(PkAvail_tb),          // Flag indicating packet is available
		.Packet(dout_tb),              // The last packet that has been received
                .sync(sync_tb)
	     );

   wire        [7:0]  packElement_tb;
   reg                DataNext_tb;
   wire               DataReady_tb;
   wire               DataOverf_tb;
   
packBuild packBuild_DUT (
                .clk(clk_tb),                 // System Clock
                .rst(rst_tb),                 // Clock synchronised reset
                
        // Downwards interface to packet processor
                .PkAvail(PkAvail_tb),         // Flag indicating packet is available
                .Packet(dout_tb),             // The next packet

        // Upwards interface to serial (or other) handler : clk Clock
                .DataVal(packElement_tb),     // Output data value

                .DataNext(DataNext_tb),       // Request for next data element
                .DataReady(DataReady_tb),     // Indicator that the next data element is available
 
                .DataOverf(DataOverf_tb)      // Too much data in buffer
                         );

   //-----------------------------------------------------------
   // Send a byte to the traceport, toggling clock appropriately
   // Two cases, sending word AhAlBhBl.
   // Data arrives with first sample in MS 4 bits, second sample in LS 4 bits.
   // When in phase (PS=0);
   //  SendA  SendB
   //   Ah     Al
   //   Bh     Bl
   //
   // When out of phase (PS=1);
   //  SendA  SendB
   //   xx     Ah
   //   Al     Bh
   //   Bl     xx

   task sendByte;
      input[7:0] byteToSend;
      integer bitsToSend;

      reg [3:0] leftover;
      reg [7:0] txbuffer;

      begin
	 bitsToSend=8;
         if (PS)
           begin
              txbuffer={byteToSend[3:0],leftover};
              leftover=byteToSend[7:4];
           end
         else
           begin
              txbuffer={byteToSend[7:4],byteToSend[3:0]};
           end
         
	 while (bitsToSend>0) begin
	    traceDinA_tb[chunksize:0]=txbuffer;
	    bitsToSend=bitsToSend-(chunksize+1);
	    txbuffer=txbuffer>>(chunksize+1);
	    traceDinB_tb[chunksize:0]=txbuffer;
	    bitsToSend=bitsToSend-(chunksize+1);
	    txbuffer=txbuffer>>(chunksize+1);
	    traceClk_tb<=0;
	    #10;
	    traceClk_tb<=1;
	    #10;
            traceClk_tb<=0;
            //$write("%x%x",traceDinA_tb,traceDinB_tb);
            
	 end
      end
   endtask
   //-----------------------------------------------------------      

   integer goodreturn;
   reg [7:0] feedval;
   integer dummy;
      
   integer fd;
   reg [8*10:1] str;

   always
     begin
	while (1)
	  begin
	     clk_tb=~clk_tb;
	     #2;
	  end
     end

   always @(posedge clk_tb)
       begin
          if (DataReady_tb)
            $display("Rx:%02X (%d)",packElement_tb,DataReady_tb);
       end

   initial begin
      rst_tb=0;
      width_tb=WIDTH;
      traceDinA_tb=0;
      traceDinB_tb=0;
      DataNext_tb=0;
      traceClk_tb=0;

      clk_tb=0;
      #1;      
      rst_tb=1;
      #1;
      rst_tb=0;
      
      // Initially send some junk while out of sync
      sendByte(8'hfe);
      sendByte(8'h23);

      // Now send Sync Sequence
      sendByte(8'hff);
      sendByte(8'hff);
      sendByte(8'hff);
      sendByte(8'h7f);
      sendByte(8'hff);
      sendByte(8'hff);
      sendByte(8'hff);
      sendByte(8'h7f);
      
      // ....and a sane message
      sendByte(8'hAA);
      if (DataReady_tb==1'b0)
	begin
	   $display("Data not available before packet complete: CORRECT");
	end
      else
	begin
	   $display("Data available signalled before packet complete: ERROR");
	end
      sendByte(8'h10);
      sendByte(8'h01);
      sendByte(8'h11);
      sendByte(8'h02);
      sendByte(8'h12);      
      sendByte(8'h03);
      sendByte(8'h13);      
      sendByte(8'h04);
      sendByte(8'h14);      
      sendByte(8'h05);
      sendByte(8'h15);      
      sendByte(8'h06);
      sendByte(8'h16);      
      sendByte(8'h07);
      sendByte(8'h18);

      sendByte(8'h20);
      sendByte(8'h01);
      sendByte(8'h21);
      sendByte(8'h02);
      sendByte(8'h22);      
      sendByte(8'h03);
      sendByte(8'h23);      
      sendByte(8'h04);
      sendByte(8'h24);      
      sendByte(8'h05);
      sendByte(8'h25);      
      sendByte(8'h06);
      sendByte(8'h26);      
      sendByte(8'h07);
      sendByte(8'h28);
      sendByte(8'h28);      


      sendByte(8'h30);
      sendByte(8'h01);
      sendByte(8'h31);
      sendByte(8'h02);
      sendByte(8'h32);
      sendByte(8'h03);
      sendByte(8'h33);
      sendByte(8'h04);
      sendByte(8'h34);
      sendByte(8'h05);
      sendByte(8'h35);
      sendByte(8'h06);
      sendByte(8'h36);
      sendByte(8'h07);
      sendByte(8'h38);
      sendByte(8'h39);
      
      sendByte(8'hff);
      sendByte(8'hff);

      sendByte(8'hff);
      sendByte(8'h7f);

      sendByte(8'haa);
      sendByte(8'hbb);

      DataNext_tb<=1;
      #1000;
      
      $finish;
      
   end
   initial begin
      $dumpfile("trace_IF.vcd");

      $dumpvars;
   end
endmodule // trace_IF_tb

   
   
