`timescale 1ns/100ps

// Run with
//  iverilog -o r spi.v ../testbeds/spi_tb.v  ; vvp r

module spi_tb;

   reg 	     clk_tb;
   reg 	     rst_tb;
   wire      tx_tb;
   reg       rx_tb;
   reg       cs_tb;
   reg       dclk_tb;

   reg [127:0] tx_packet_tb;

   wire        txGetNext_tb;
   wire        pkt_complete_tb;
   wire [127:0] rxedFrame_tb;
       
spi spi_DUT (
	    .rst(rst_tb),
             .clk(clk_tb),
             
	    .Tx(tx_tb),
	    .Rx(rx_tb),
            .Cs(cs_tb),
	    .DClk(dclk_tb),

	    .Tx_packet(tx_packet_tb),
            .TxGetNext(txGetNext_tb),
             
            .PktComplete(pkt_complete_tb),
            .rxedFrame(rxedFrame_tb)
	    );

   always
     begin
	while (1)
	  begin
	     clk_tb=~clk_tb;
	     #2;
	  end
     end

   always
     begin
        while (1)
          begin
             if (cs_tb==0)
               dclk_tb=!dclk_tb;
             else
               dclk_tb=1;

             #5;
          end
     end
      
   initial begin
      rst_tb=0;
      clk_tb=0;
      tx_packet_tb=128'h0FF101010FF101010101010101010101;
      cs_tb=1;
      rx_tb=0;
      
      #10;
      rst_tb=1;
      #10;
      rst_tb=0;
      #20;

      
      
      cs_tb=0;
      #1400;
      cs_tb=1;
      #20;
      $finish;
      
      
   end

   initial begin
      $dumpfile("trace_IF.vcd");

      $dumpvars;
   end
endmodule // spi_tb

   
