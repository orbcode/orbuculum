`default_nettype none

// frameToSerial
// ============
//
// Take a frame from the fifo and serialise it to the output device. Send
// sync frames at intervals, irrespective of if there is data or not.
//
module frameToSerial (
		input            clk,
		input            rst,

        // Command and Control
                output reg [1:0] Width,            // Width setting to hardware

        // Downwards interface to frame buffer
		input [127:0]    Frame,            // Input frame
		output reg       FrameNext,        // Request for next data frame
		input            FrameReady,       // Next deMW is available
                input [BUFFLENLOG2-1:0] FramesCnt, // No of frames available                     

	// Upwards interface to serial (or other) handler : clk Clock
		output reg [7:0] DataVal,          // Output data value
		input            DataNext,         // Request for next data element
		output reg       DataReady,        // Next data element is available

                input            RxedEvent,        // Flag that something was received
                input [7:0]      DataInSerial,     // Data we received over serial link

        // Stats out
                input [7:0]        Leds,           // Led values on the board
                input [15:0]       LostFrames,     // Number of frames lost
                input [31:0]       TotalFrames     // Number of frames received
 		);

   parameter SYNC_INTERVAL=2;
   
   // Internals =========================================================================
   parameter BUFFLENLOG2=9;
   
   reg [127:0]                   currentFrame;     // The frame being transmitted

   reg [4:0]                     count;            // Which element we are outputting?
   reg                           senderState;      // state encoding for uploader
   reg [22:0]                    syncInterval;     // Interval between sync frames
   reg [127:0]                   frameHeader;
   reg                           primed;           // State toggle for serial rx

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
             primed       <= 0;
             Width        <= 2'h3;
	  end
	else
	  begin
             if (syncInterval!=0) syncInterval <= syncInterval - 1;

             // Send data to uart if possible ==========================================
             case (senderState)
               ST_IDLE: // -- Waiting for some data to become available --
                 begin
                    if ((FrameReady) || (syncInterval==0))
                      begin
                         count<=5'h10;
                         senderState<=ST_SENDING_FRAME;
                         if (syncInterval==0)
                           begin
                              syncInterval <= ~0;
                              currentFrame <= {8'hA6,(16-BUFFLENLOG2)'h0,FramesCnt, 16'h0, Leds, LostFrames, TotalFrames, 32'hFFFFFF7F };
                           end
                         else
                           begin
                              currentFrame<=Frame;
                              FrameNext<=1'b1;
                           end
                      end
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

             // Check for command back from UART to change width ====================
             if (RxedEvent)
               begin
                  primed<=1'b0;
                  /* If character is a 'w' then prime for second character */
                  if (DataInSerial==8'h77) primed<=1'b1;
                  else
                    begin
                       if (primed==1'b1)
                         case (DataInSerial)
                           8'hA0: Width<=0;
                           8'hA1: Width<=1;
                           8'hA2: Width<=2;
                           8'hA3: Width<=3;
                           default: Width<=Width;
                         endcase // case (DataInSerial)
                    end // else: !if(DataInSerial==8'h77)
               end // if (RxedEvent)
          end // else: !if(rst)
     end // always @ (clk,posedge rst)
endmodule // frameToSerial
