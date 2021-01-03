// TPIU Signaling framework. 
// From CoreSight TPIU-Lite Technical Reference Manual Revision: r0p0 

`timescale 1ns/100ps

// Tests for traceIF.
// Set width to 3, 2 and 1 to test each width
// Run with
//  iverilog -o r traceIF.v ../testbeds/traceIF_tb.v  ; vvp r

module traceIF_tb;
   parameter WIDTH=3;    // 3=4 bit, 2=2 bit, 1,0=1 bit

   parameter chunksize=(WIDTH==3)?3:(WIDTH==2)?1:0;

   // Testbed interface 
   reg [3:0] traceDinA_tb;
   reg [3:0] traceDinB_tb;
   reg [1:0] width_tb;
   
   reg 	     traceClk_tb;
   reg 	     clk_tb;
   reg 	     rst_tb;

   wire      dAvail_tb;
   reg       dAck_tb;
   
   wire [127:0] dout_tb;
   wire        sync_tb;
   
traceIF DUT (
		.rst(rst_tb),                  // Reset synchronised to clock

	// Downwards interface to the trace pins
		.traceDina(traceDinA_tb),      // Tracedata rising edge... 1-n bits max, can be less
		.traceDinb(traceDinB_tb),      // Tracedata falling edge... 1-n bits max, can be less
		.traceClkin(traceClk_tb),      // Tracedata clock... async to clk
		.width(width_tb),              // How wide the bus under consideration is 0..3 (1, 1, 2 & 4 bits)

	// Upwards interface to packet processor
		.PkAvail(dAvail_tb),           // Flag indicating packet is available
		.Packet(dout_tb)               // The last packet that has been received
	     );
   

   //-----------------------------------------------------------
   // Send a byte to the traceport, toggling clock appropriately
   // Fortunately, CORTEX-M always sends LSB on L-H edge, so we don't
   // need to worry about phase.
   //  SendA  SendB
   //   Al     Ah
   //   Bl     Bh

   task sendByte;

      input[7:0] byteToSend;
      integer bitsToSend;

      reg [7:0] txbuffer;

      begin
	 bitsToSend=8;
         txbuffer={byteToSend[7:4],byteToSend[3:0]};

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

   reg [2:0] davail_cdc;

   always @(posedge clk_tb)
     begin
        davail_cdc<={davail_cdc[1:0],dAvail_tb};
        if (davail_cdc==3'b011)
          begin
             $display("OUTPUT=%x",dout_tb);
             dAck_tb<=1'b1;
          end
        else
          dAck_tb<=1'b0;
     end

   always
     begin
	while (1)
	  begin
	     clk_tb=~clk_tb;
	     #2;
	  end
     end
      
   initial begin
      rst_tb=0;
      width_tb=WIDTH;
      traceDinA_tb=0;
      traceDinB_tb=0;      
      traceClk_tb=0;
      clk_tb=0;
      #10;
      rst_tb=1;
      #10;
      rst_tb=0;
      #20;

      // Initially send some junk while out of sync
      sendByte(8'h00);
      sendByte(8'h00);
      sendByte(8'h00);
      sendByte(8'h99);
      sendByte(8'h88);

      // Now send Sync Sequence
      sendByte(8'hff);
      sendByte(8'hff);
      sendByte(8'hff);
      sendByte(8'h7f);

      // ....and a sane message
      sendByte(8'h12);
      sendByte(8'h34);
      
      sendByte(8'h02);
      sendByte(8'h03);
      
      sendByte(8'h04);
      sendByte(8'h05);   
         
      sendByte(8'h06);
      sendByte(8'h07); 

      sendByte(8'h08);
      sendByte(8'h09);
      
      sendByte(8'h0a);
      sendByte(8'h0b);
      
      sendByte(8'h0c);
      sendByte(8'h0d);

      sendByte(8'h0e);
      sendByte(8'h0f);
      sendByte(8'h0e);
      sendByte(8'h0f);
      sendByte(8'h0e);
      sendByte(8'h0f);
      sendByte(8'h0e);
      sendByte(8'h0f);            

      $finish;
      
   end
   initial begin
      $dumpfile("trace_IF.vcd");

      $dumpvars;
   end
endmodule // trace_IF_tb

   
   
