// TPIU Signaling framework. 
// From CoreSight TPIU-Lite Technical Reference Manual Revision: r0p0 

`timescale 1ns/100ps

// Tests for traceIF in combination with packBuild.
// Set width to 3, 2 and 1 to test each width
// Set PS to 0 and 1 to test phase differences
// Run with
//  iverilog -o r traceIF.v packBuild.v ../testbeds/traceIF_tb.v  ; vvp r

module packbuild_tb;
   parameter WIDTH=3;    // 3=4 bit, 2=2 bit, 1,0=1 bit
   parameter PS=1;       // If to implement phase shift

   parameter chunksize=(WIDTH==3)?3:(WIDTH==2)?1:0;

   // Testbed interface 
   reg[3:0] traceDinA_tb;
   reg[3:0] traceDinB_tb;
   reg [1:0] width_tb;
   
   reg 	    traceClk_tb;
   reg 	    clk_tb;
   reg 	    rst_tb;

   wire        dAvail_tb;
   wire [15:0] dout_tb;
   wire        pr_tb;
   
   
traceIF traceIF_DUT (
		.rst(rst_tb),                  // Reset synchronised to clock

	// Downwards interface to the trace pins
		.traceDina(traceDinA_tb),      // Tracedata rising edge... 1-n bits max, can be less
		.traceDinb(traceDinB_tb),      // Tracedata falling edge... 1-n bits max, can be less
		.traceClkin(traceClk_tb),      // Tracedata clock... async to clk
		.width(width_tb),              // How wide the bus under consideration is 0..3 (1, 1, 2 & 4 bits)

	// Upwards interface to packet processor
		.WdAvail(dAvail_tb),           // Flag indicating word is available
		.PacketWd(dout_tb),            // The last word that has been received
		.PacketReset(pr_tb)            // Flag indicating to start packet again
	     );

   wire        [15:0] packElement_tb;
   reg                DataNext_tb;
   wire               DataReady_tb;
   reg                DataFrameReset_tb;
   wire               FrameReady_tb;
   wire               DataOverf_tb;
   
packBuild packBuild_DUT (
                .clk(clk_tb), // System Clock
                .rst(rst_tb), // Clock synchronised reset
                
                // Downwards interface to packet processor : wrClk Clock
                .wrClk(traceClk_tb), // Clock for write side operations to fifo
                .WdAvail(dAvail_tb), // Flag indicating word is available
                .PacketReset(pr_tb), // Flag indicating to start again
                .PacketWd(dout_tb), // The next packet word

                // Upwards interface to serial (or other) handler : clk Clock
                .DataVal(packElement_tb), // Output data value

                .rdClk(clk_tb),
                .DataNext(DataNext_tb), // Request for next data element
                .DataReady(DataReady_tb), // Indicator that the next data element is available
                .DataFrameReset(DataFrameReset_tb), // Reset to start of data frame
                .FrameReady(FrameReady_tb), // Indicator that a complete frame of data is available
 
                .DataOverf(DataOverf_tb) // Too much data in buffer
                         );

   always @(negedge traceClk_tb)
     begin
        if (pr_tb)
          $monitor("***PACKET RESET***========================");
        else
          begin 
             if (dAvail_tb==1)
               begin
                  //if (dout_tb!=16'h7fff)
                    $display("%04x",dout_tb);
               end // if (dAvail_tb==1)
          end // else: !if(pr_tb)
     end // always @ (posedge traceClk_tb)
      
   //-----------------------------------------------------------
   // Send a byte to the traceport, toggling clock appropriately   
   task sendByte;
      input[7:0] byteToSend;
      integer bitsToSend;

      reg [3:0] leftover;
      integer   sendbuffer;

      // In the coded files, 4MSB are the LSB, and vice-versa, so reverse them
      begin
	 bitsToSend=8;
         if (PS)
           begin
              sendbuffer=byteToSend;           
              byteToSend={leftover,byteToSend[7:4]};
              leftover=sendbuffer[3:0];
           end
         
	 while (bitsToSend>0) begin
	    traceDinB_tb[chunksize:0]=byteToSend;
	    bitsToSend=bitsToSend-(chunksize+1);
	    byteToSend=byteToSend>>(chunksize+1);
	    traceDinA_tb[chunksize:0]=byteToSend;
	    bitsToSend=bitsToSend-(chunksize+1);
	    byteToSend=byteToSend>>(chunksize+1);
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
      fd = $fopen("/home/dmarples/Develop/orbuculum/orbtrace/stimfiles/fastitm.dat","r");
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

   
   
