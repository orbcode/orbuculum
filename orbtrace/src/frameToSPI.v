`default_nettype none

// frameToSPI
// ==========
//
// Take a frame from the fifo and prepare it for the output device. Collections of
// frames have a header and footer attached, which contain the same information;
//
// <A6> <SendCount> <SyncCount> <LEDs> 00 <LostFrames> <TotalFrames> FFFFFFF7
//
// <SendCount>   - Number of frames to be send in this cluster
// <SyncCount>   - 16 bit count of number of sync packets received
// LEDs          - 8 bit LEDs values from the board
// <LostFrames>  - 16 bit count of number of lost frames
// <TotalFrames> - 32 bit count of number of frames received (inc lost ones)
// FFFFFFF7      - Sync Reset
//
// At the foot of the cluster this packet contains the configuration for the _next_
// cluster.
//

module frameToSPI (
		input            clk,
		input            rst,
		
        // Command and Control
                output reg [1:0] Width,            // Trace port width
                output           Transmitting,     // Indication of transmit activity

        // Downwards interface to packet buffer
		input [127:0]    Frame,            // Input frame
		output reg       FrameNext,        // Request for next data element
		input            FrameReady,       // Next data element is available
                input [BUFFLENLOG2-1:0] FramesCnt, // No of frames available

	// Upwards interface to SPI
		output reg    [127:0] TxFrame,        // Output data frame
                input              TxGetNext,      // Toggle to request next frame

		input [31:0]       RxPacket,       // Input data packet
		input              PktComplete,    // Request for next data element
                input              CS,             // Current Chip Select state

        // Stats out
                input [7:0]        Leds,           // Led values on the board
                input [15:0]       LostFrames,     // Number of frames lost
                input [31:0]       TotalFrames     // Number of frames received
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
   reg [1:0]                       senderState;      // State encoding for uploader
   reg [2:0]                       txGetNext_toggle; // Next packet tx edge detect
   reg [2:0]                       rxGotNext_toggle; // Next packet rx edge detect

   /* Decodes for startup frame */
   wire [7:0]                      command     = RxPacket[31 :24];
   wire [1:0]                      pkWidth     = RxPacket[17 :16];
   wire [15:0]                     pksendCount = RxPacket[15 : 0];

   assign Transmitting=(senderState!=ST_IDLE);
   
   always @(posedge clk,posedge rst)
     begin
        FrameNext      <= 1'b0;        // Not currently requesting a new frame
        
	if (rst)  // Perform reset actions ==============================================
	  begin
             senderState  <= ST_IDLE;
             rxGotNext_toggle <= 0;
             txGetNext_toggle <= 0;  
             Width<=3;              // Default is a 4 bit bus
	  end
	else
	  begin
             TxFrame <= (senderState==ST_SENDING_FRAMES)?Frame:{8'hA6,(16-BUFFLENLOG2)'h0,FramesCnt, 16'h0, Leds, LostFrames, TotalFrames, 32'hFFFFFF7F };
             txGetNext_toggle <= { txGetNext_toggle[1:0], TxGetNext };
             rxGotNext_toggle <= { rxGotNext_toggle[1:0], PktComplete };

             /* One data element must always be sent because we don't have the received */
             /* frame at the point the tx frame is to go. We make that first frame a    */
             /* repitition of the footer frame.                                         */
             
             // Send data to spi if possible ============================================
             case (senderState)
               ST_IDLE: // -- Waiting for a request to send  --
                 begin
                    /* Check for Start sending frames request */
                    if ((CS==1'b0) &&
                        ((rxGotNext_toggle==3'b100) || (rxGotNext_toggle==3'b011)))
                      begin
                         senderState <= ST_SENDING_FIRST;
                      end
                 end

               ST_SENDING_FIRST: // -- Outputting first frame of the block --
                    if (CS==1'b1)
                      begin
                         sendCount <= FramesCnt;
                         senderState <= ST_IDLE;
                      end
                    else
                      begin
                         if ((txGetNext_toggle==3'b100) || (txGetNext_toggle==3'b011))
                           begin
                              Width <= pkWidth;
                              if (pksendCount == 0)
                                begin
                                   sendCount <= FramesCnt;                                   
                                   senderState <= ST_SENDING_EXHAUST;
                                end
                              else
                                begin
                                   sendCount <= pksendCount;
                                   senderState <= ST_SENDING_FRAMES;
                                end
                           end
                      end
                 
               ST_SENDING_FRAMES: // -- Outputting the frames --
                 begin
                    if (CS==1'b1)
                      begin
                         sendCount <= FramesCnt;                         
                         senderState <= ST_IDLE;
                      end
                    else
                      begin
                         if ((txGetNext_toggle==3'b100) || (txGetNext_toggle==3'b011))
                           begin
                              sendCount <= sendCount-1;
                              FrameNext <= 1'b1;

                              /* Move to exhaust state when sendCount==1 due to pipelining */
                              if (sendCount==1)
                                begin
                                   sendCount<=FramesCnt-1;
                                   senderState <= ST_SENDING_EXHAUST;
                                end
                           end
                      end // else: !if(CS==1'b1)
                 end // case: ST_SENDING_FRAMES
                                
               ST_SENDING_EXHAUST: // -- Outputting status info until CS is disserted --
                 begin
                    if (CS==1'b1) senderState <= ST_IDLE;
                 end
               
             endcase // case (senderState)
          end // else: !if(rst)
     end // always @ (posedge clk,posedge rst)
endmodule // packToSPI
