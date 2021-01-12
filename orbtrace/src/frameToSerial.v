`default_nettype none

// frameToSerial
// ============
//
// Take a frame from the fifo and serialise it to the output device. Intersperse the
// flow with sync frames at a parameterised interval.
//
module frameToSerial (
		input            clk,
		input            rst,
		
        // Downwards interface to frame buffer
		input [127:0]    Frame,            // Input frame
		output reg       FrameNext,        // Request for next data frame
		input            FrameReady,       // Next deMW is available
                input [BUFFLENLOG2-1:0] FramesCnt, // No of frames available                     

	// Upwards interface to serial (or other) handler : clk Clock
		output reg [7:0] DataVal,          // Output data value
		input            DataNext,         // Request for next data element
		output reg       DataReady,        // Next data element is available

        // Stats out
                input [7:0]        Leds,           // Led values on the board
                input [15:0]       SyncCount,      // Number of syncs detected
                input [15:0]       LostFrames,     // Number of frames lost
                input [31:0]       TotalFrames     // Number of frames received
 		);

   parameter SYNC_INTERVAL=2;
   
   // Internals =========================================================================
   parameter BUFFLENLOG2=9;
   
   reg [127:0]                   currentFrame;     // The frame being transmitted

   reg [4:0]                     count;            // Which element we are outputting?
   reg                           senderState;      // state encoding for uploader
   reg [7:0]                     syncInterval;     // Interval between sync frames
   reg [127:0]                   frameHeader;
   
   
   /* States for uploader state machine */
   parameter
     ST_IDLE           = 0,
     ST_SENDING_FRAME  = 1;

   always @(posedge clk,posedge rst)
     begin
        DataReady      <= 1'b0;        // No data waiting to send
        FrameNext      <= 1'b0;        // Not currently requesting a new frame

	if (rst)  // Perform reset actions ==============================================
	  begin
             senderState  <= ST_IDLE;
             syncInterval <= 0;
	  end
	else
	  begin
             // Send data to uart if possible ==========================================
             case (senderState)
               ST_IDLE: // -- Waiting for some data to become available --
                 begin
                    if (FrameReady)
                      begin
                         count<=5'h10;
                         senderState<=ST_SENDING_FRAME;
                         if (syncInterval==0)
                           begin
                              syncInterval <= SYNC_INTERVAL;
                              currentFrame <= {8'hA6,(16-BUFFLENLOG2)'h0,FramesCnt, SyncCount, Leds, LostFrames, TotalFrames, 32'hFFFFFF7F };
                           end
                         else
                           begin
                              currentFrame<=Frame;
                              FrameNext<=1'b1;
                              syncInterval <= syncInterval - 1;
                           end // else: !if(syncInterval==0)
                      end // if (FrameReady)
                 end // case: ST_IDLE

               ST_SENDING_FRAME: // -- Outputting the frame --
                 begin
                    if (count==0) senderState <= ST_IDLE;
                    else
                      begin
                         if (DataNext)
                           begin
                              DataReady     <= 1'b1;
                              DataVal       <= currentFrame[127 -: 8];
                              count         <= count-1;
                              currentFrame  <= {currentFrame[119:0],8'h0};
                           end
                      end // case: senderState[ST_SENDING_FRAME]
                 end // case: ST_SENDING_FRAME
             endcase // case (senderState)
          end // else: !if(rst)
     end // always @ (clk,posedge rst)
endmodule // frameToSerial
