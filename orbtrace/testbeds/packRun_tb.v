// TPIU Signaling framework. 
// From CoreSight TPIU-Lite Technical Reference Manual Revision: r0p0 

`timescale 1ns/100ps

// Tests for packet Buffering on real data (built on traceIF)
// Set width to 3, 2 and 1 to test each width
// Run with
//  iverilog -o r traceIF.v packBuffer.v ram.v ../testbeds/packRun_tb.v  ; vvp r

module packRun_tb;
   parameter WIDTH=4;          // 3=4 bit, 2=2 bit, 1,0=1 bit
   parameter BUFFLENLOG2=9;    // Depth of packet frame buffer (512 bytes)   

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

traceIF traceIF_tb (
		.rst(rst_tb),                  // Reset synchronised to clock

	// Downwards interface to the trace pins
		.traceDina(traceDinA_tb),      // Tracedata rising edge... 1-n bits max, can be less
		.traceDinb(traceDinB_tb),      // Tracedata falling edge... 1-n bits max, can be less
		.traceClkin(traceClk_tb),      // Tracedata clock... async to clk
		.width(width_tb),              // How wide the bus under consideration is 0..3 (1, 1, 2 & 4 bits)

	// Upwards interface to packet processor
		.PkAvail(PkAvail_tb),          // Flag indicating packet is available
		.Packet(dout_tb)               // The last packet that has been received
	     );

   wire [127:0] frame_tb;
   reg          frameNext_tb;
   wire [BUFFLENLOG2-1:0] framesCnt_tb;          // No of frames available   
   wire         frameReady_tb = (framesCnt_tb!=0);
   wire         dataOverf_tb;

packBuffer #(.BUFFLENLOG2(BUFFLENLOG2)) packBuffer_DUT (
                .clk(clk_tb),
                .rst(rst_tb),
                
        // Downwards interface to packet processor
                .PkAvail(PkAvail_tb),           // Flag indicating packet is available
                .Packet(dout_tb),               // The next packet
                  
		// Upwards interface to output handler : clk Clock
		.Frame(frame_tb),               // Output frame

		.FrameNext(frameNext_tb),       // Request for next data element
		.FramesCnt(framesCnt_tb),       // Number of data elements available

		.DataOverf(dataOverf_tb)        // Too much data in buffer
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
   reg     readTime;
   
      
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
        frameNext_tb<=1'b0;

        if (readTime==1) // Slight bodge to allow 1 cycle for frameNext to re-sync.
          readTime=0;
        else        
          if (frameReady_tb!=0)
            begin
               frameNext_tb<=1'b1;
               readTime=1;
               $display("Rx:%16X",frame_tb);
            end
       end

   initial begin
      rst_tb=0;
      readTime=0;
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
      fd = $fopen("../stimfiles/fastitm.dat","r");
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
endmodule // packRun_tb

