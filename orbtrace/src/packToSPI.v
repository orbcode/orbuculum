`default_nettype none

// packToSPI
// =========
//
module packToSPI (
		input            clk,
		input            rst,
		
        // Commands output
                output reg [1:0] Width,            // Trace port width
                output           Transmitting,     // Indication of transmit activity

        // Downwards interface to packet buffer
		input [127:0]    Frame,            // Input frame
		output reg       FrameNext,        // Request for next data element
		input            FrameReady,       // Next data element is available
                input [BUFFLENLOG2-1:0] FramesCnt, // No of frames available

	// Upwards interface to SPI
		output     [127:0] TxPacket,       // Output data packet
                input              TxGetNext,      // Toggle to request next packet

		input [31:0]       RxPacket,       // Input data packet
		input              PktComplete,    // Request for next data element
                input              CS              // Current Chip Select state
 		);

   
   // Internals =========================================================================
   parameter BUFFLENLOG2=9;
   
   /* States for uploader state machine */
   parameter
     ST_IDLE            = 0,
     ST_SENDING_FIRST   = 1,
     ST_SENDING_FRAMES  = 2,
     ST_SENDING_EXHAUST = 3;

   reg [127:0]                     frame;
   reg [15:0]                      sendCount;
   reg [3:0]                       senderState;      // Onehot state encoding for uploader
   reg [2:0]                       txGetNext_toggle; // Next packet tx edge detect
   reg [2:0]                       rxGotNext_toggle; // Next packet rx edge detect

   /* Frame header/footer is <NumToSend> ..... (any just we want) <Footer> */
   wire [119:0]                    frameHeader = {(16-BUFFLENLOG2)'h0,sendCount, 8'hff,60'h0,senderState, 32'hFFFFFF7F };

   /* Decodes for startup frame */
   wire [7:0]                      command     = RxPacket[31 :24];
   wire [1:0]                      pkWidth     = RxPacket[17 :16];
   wire [15:0]                     pksendCount = RxPacket[15 : 0];

   /* Frame needs to be reversed to go out of SPI port correctly */
   wire [127:0]                    revFrame = { Frame[  7 -: 8], Frame[ 15 -: 8], Frame[ 23 -: 8], Frame[ 31 -: 8],
                                                Frame[ 39 -: 8], Frame[ 47 -: 8], Frame[ 55 -: 8], Frame[ 63 -: 8],
                                                Frame[ 71 -: 8], Frame[ 79 -: 8], Frame[ 87 -: 8], Frame[ 95 -: 8],
                                                Frame[103 +: 8], Frame[111 -: 8], Frame[119 -: 8], Frame[127 -: 8] };
   
   /* If we're not sending exhaust then we just relay the send packet */
   assign TxPacket=(senderState==(1<<ST_SENDING_FRAMES))?revFrame:{ 8'hA6, frameHeader };
   // For test purposes, replace Frame with {RxPacket,3'h0,senderState,2'h0,Width, sendCount};

   assign Transmitting=(senderState!=(1<<ST_IDLE));
   
   always @(posedge clk,posedge rst)
     begin
        FrameNext      <= 1'b0;        // Not currently requesting a new frame
        
	if (rst)  // Perform reset actions ==============================================
	  begin
             senderState  <= (1<<ST_IDLE);
             rxGotNext_toggle <= 0;
             txGetNext_toggle <= 0;  
             Width<=3;              // Default is a 4 bit bus
	  end
	else
	  begin
             txGetNext_toggle <= { txGetNext_toggle[1:0], TxGetNext };
             rxGotNext_toggle <= { rxGotNext_toggle[1:0], PktComplete };

             /* One data element must always be sent because we don't have the received */
             /* frame at the point the tx frame is to go. We make that first frame a    */
             /* repitition of the footer frame.                                         */
             
             // Send data to spi if possible ============================================
             case (senderState)
               (1<<ST_IDLE): // -- Waiting for a request to send  --
                 begin
                    /* Check for Start sending frames request */
                    if ((CS==1'b0) &&
                        ((rxGotNext_toggle==3'b100) || (rxGotNext_toggle==3'b011)))
                      begin
                         senderState<=(1<<ST_SENDING_FIRST);
                      end
                 end

               (1<<ST_SENDING_FIRST): // -- Outputting first frame of the block --
                    if (CS==1'b1)
                      begin
                         sendCount<=FramesCnt;
                         senderState <= (1<<ST_IDLE);
                      end
                    else
                      begin
                         if ((txGetNext_toggle==3'b100) || (txGetNext_toggle==3'b011))
                           begin
                              Width<=pkWidth;
                              FrameNext<=1'b1;
                              if (pksendCount==0)
                                begin
                                   sendCount<=FramesCnt;                                   
                                   senderState <= (1<<ST_SENDING_EXHAUST);
                                end
                              else
                                begin
                                   sendCount<=pksendCount;
                                   senderState <= (1<<ST_SENDING_FRAMES);
                                end
                           end
                      end
                 
               (1<<ST_SENDING_FRAMES): // -- Outputting the frames --
                 begin
                    if (CS==1'b1)
                      begin
                         sendCount<=FramesCnt;                         
                         senderState <= (1<<ST_IDLE);
                      end
                    else
                      begin
                         if ((txGetNext_toggle==3'b100) || (txGetNext_toggle==3'b011))
                           begin
                              sendCount<=sendCount-1;
                              FrameNext<=1'b1;

                              /* Move to exhaust state when sendCount==1 due to pipelining */
                              if (sendCount==1)
                                begin
                                   sendCount<=FramesCnt;                                   
                                   senderState <= (1<<ST_SENDING_EXHAUST);
                                end
                           end
                      end // else: !if(CS==1'b1)
                 end // case: (1<<ST_SENDING_FRAMES)
                                
               (1<<ST_SENDING_EXHAUST): // -- Outputting status info until CS is disserted --
                 begin
                    if (CS==1'b1)
                      begin
                         senderState <= (1<<ST_IDLE);
                      end
                 end
               
             endcase // case (senderState)
          end // else: !if(rst)
     end // always @ (posedge clk,posedge rst)
endmodule // packToSPI
