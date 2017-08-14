`timescale 1ns/100ps

module uart_tb_tb();
   reg 	    clk;
   reg 	    nRst;
   wire      nDValid_tb;
   wire [7:0] dOut_tb;
   wire       nSync_tb;

   wire [7:0] rx_byte_tl;
   
   
   integer  w;
   integer  simcount=0;

uartTB #(.CLOCKFRQ(240000000)) uart_tb (
	    .clk(clk),
	    .nRst(nRst),
	    .nDValid(nDValid_tb),
	    .dOut(dOut_tb)
   );

    uart #(.CLOCKFRQ(240000000))  receiver (
	.clk(clk),
	.nRst(nRst),
	.rx(uartrx),
	.tx(uarttx),
	.transmit(~nDValid_tb),
	.tx_byte(dOut_tb),
	.received(rxTrig_tl),
	.rx_byte(rx_byte_tl),
	.is_receiving(rxInd),
	.is_transmitting(txInd),
	.recv_error(rxErr_tl)
    );
   
   always
     begin
	while (1)
	  begin
	     #2;
	     clk=~clk;
	  end
     end
      
   initial begin
      clk=0;
      nRst=1;
      #5;
      nRst=0;
      #10;
      nRst=1;
      #1500000;

      $finish;
      
   end
   initial begin
      $dumpfile("trace_IF.vcd");

      $dumpvars;
   end
endmodule // trace_IF_tb

   
   
