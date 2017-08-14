
module topLevel(
		input [3:0] traceDin, // Port is always 4 bits wide, even if we use less
		input 	    traceClk, // Supporting clock
		input 	    uartrx, // Receive data into UART

		output 	    uarttx, // Transmit data from UART 


		output 	    sync_led,
		output 	    rxInd_led, // Received UART Data indication
		output 	    txInd_led, // Transmitted UART Data indication
		output reg  x,
		output 	    nDv,
		input 	    clkIn,
		input 	    rst,
		output 	    txOvf_led,
		output 	    D6,
		output 	    D5,
		output 	    D4,
		output 	    D3,
		output      cts 	    
		);
   

   // Parameters =============================================================================
   parameter CHUNKSIZE=(4-1);
   
   // Internals =============================================================================

   reg [1:0] 		   width_tb;
   reg 			   nDv;

   reg 			   clk;

 			   
   wire                    dvalid_tl;
   wire [7:0]		   dOut_tl;
   wire 		   rxErr_tl;
   wire 		   rxTrig_tl;
   wire [7:0] 		   rx_byte_tl;

   traceIF target (
                .traceDin(traceDin),
                .width(width_tb),
                .traceClk(traceClk),
                .clk(clk),
                .nRst(~rst),
		.dvalid(dvalid_tl),  
		.dOut(dOut_tl),
		.sync(sync_led)
                );


/*
 uartTB #(.CLOCKFRQ(70500000)) uart_tb (
					.clk(clk),
					.nRst(~rst),
					.nDValid(nDvalid_tl),
					.dOut(dOut_tl)
					);
*/
   uart #(.CLOCKFRQ(70500000))  receiver (
	.clk(clk),
	.nRst(~rst),
	.rx(uartrx),
	.tx(uarttx),
	.transmit(dvalid_tl),
	.tx_byte(dOut_tl),
	.received(rxTrig_tl),
	.rx_byte(rx_byte_tl),
	.is_receiving(rxInd_led),
	.is_transmitting(txInd_led),
	.recv_error(rxErr_tl),
        .tx_overf(txOvf_led)
    );

   // Set up clock for 70.5Mhz with input of 12MHz
   SB_PLL40_CORE #(
		   .FEEDBACK_PATH("SIMPLE"),
		   .PLLOUT_SELECT("GENCLK"),
		   .DIVR(4'b0000),
		   .DIVF(7'b0101110),
		   .DIVQ(3'b011),
		   .FILTER_RANGE(3'b001)
		   ) uut (
			  .LOCK(lock),
			  .RESETB(1'b1),
			  .BYPASS(1'b0),
			  .REFERENCECLK(clkIn),
			  .PLLOUTCORE(clk)
			  );

   assign x=uartrx;
   
   

   always @(negedge rst)
     begin
	D6<=1'b0;
	D5<=1'b0;
	D4<=1'b0;
	D3<=1'b0;
	cts<=1'b0;
     end
   
   assign  nDv=rxTrig_tl;
   assign width_tb = 2'b11;   

endmodule // topLevel
