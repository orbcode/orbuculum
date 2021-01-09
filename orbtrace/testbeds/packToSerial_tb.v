// TPIU Signaling framework. 
// From CoreSight TPIU-Lite Technical Reference Manual Revision: r0p0 

`timescale 1ns/100ps

// Tests for traceIF in combination with packBuild.
// Set width to 3, 2 and 1 to test each width
// Run with
//  iverilog -o r traceIF.v packToSerial.v ram.v ../testbeds/packToSerial_tb.v  ; vvp r

module packtoserial_tb;
   parameter WIDTH=3;    // 3=4 bit, 2=2 bit, 1,0=1 bit

   parameter chunksize=(WIDTH==3)?3:(WIDTH==2)?1:0;

   // Testbed interface 
   reg[3:0] traceDinA_tb;
   reg[3:0] traceDinB_tb;
   reg [1:0] width_tb;
   
   reg 	    traceClk_tb;
   reg 	    clk_tb;
   reg 	    rst_tb;

   wire         dAvail_tb;
   wire [127:0] dout_tb;
   reg          dAck_tb;

traceIF traceIF_DUT (
		.rst(rst_tb),                  // Reset synchronised to clock

	// Downwards interface to the trace pins
		.traceDina(traceDinA_tb),      // Tracedata rising edge... 1-n bits max, can be less
		.traceDinb(traceDinB_tb),      // Tracedata falling edge... 1-n bits max, can be less
		.traceClkin(traceClk_tb),      // Tracedata clock... async to clk
		.width(width_tb),              // How wide the bus under consideration is 0..3 (1, 1, 2 & 4 bits)

	// Upwards interface to packet processor
		.PkAvail(dAvail_tb),           // Flag indicating packet is available
		.Packet(dout_tb),              // The last packet that has been received
		.PkAck(dAck_tb)                // Flag indicating ack of data packet
	     );

   wire        [7:0] packElement_tb;
   reg                DataNext_tb;
   wire               DataReady_tb;
   wire               DataOverf_tb;
   
packBuild packBuild_DUT (
                .clk(clk_tb),                  // System Clock
                .rst(rst_tb),                  // Clock synchronised reset
                
         // Downwards interface to packet processor
                .PkAvail(dAvail_tb),           // Flag indicating packet is available
                .Packet(dout_tb),              // The next packet
                .PkAck(dAck_tb),               // Ack the packet reception

         // Upwards interface to serial (or other) handler : clk Clock
                .DataVal(packElement_tb),      // Output data value

                .DataNext(DataNext_tb),        // Request for next data element
                .DataReady(DataReady_tb),      // Indicator that the next data element is available
 
                .DataOverf(DataOverf_tb)       // Too much data in buffer
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
      
   initial begin
      rst_tb=0;
      width_tb=WIDTH;
      traceDinA_tb=0;
      traceDinB_tb=0;      
      traceClk_tb=0;
      clk_tb=0;
      #1;      
      rst_tb=1;
      #1;
      rst_tb=0;
      
      goodreturn=1;
      fd = $fopen("../../stimfiles/fastitm.dat","r");
      while (goodreturn)
        begin
           goodreturn=$fgets(str,fd);
           dummy=$sscanf(str,"%d",feedval);
           sendByte(feedval);           
        end
      $finish;
      
   end
   initial begin
      $dumpfile("trace_IF.vcd");

      $dumpvars;
   end
endmodule // trace_IF_tb

   
   
