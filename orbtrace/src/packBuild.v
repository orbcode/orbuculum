`default_nettype none

// packBuild
// =========
//
module packBuild (
		input            clk,           // System Clock
		input            rst,           // Clock synchronised reset
		
	        // Downwards interface to packet processor
		input            PkAvail,       // Flag indicating word is available
		input [127:0]    Packet,        // The next packet word
                input            sync,          // A sync pulse has arrived
                  
		// Upwards interface to serial (or other) handler : clk Clock
		output reg [7:0] DataVal,       // Output data value

		input            DataNext,      // Request for next data element
		output reg       DataReady,     // Next data element is available
                output           syncInd,       // Indicator of sync status

                // Indicator that buffer overflowed
		output reg       DataOverf      // Too much data in buffer
 		);

   // Internals =========================================================================
   parameter BUFFLENLOG2=9;

   reg [BUFFLENLOG2-1:0]	 Rp;               // Read Element position in buffer
   reg [BUFFLENLOG2-1:0]	 Wp;               // Write Element position in buffer
   reg [BUFFLENLOG2-1:0]	 nextWp;           // Next Write Element position

   wire [127:0]                  ramData;          // The data from the RAM
   reg [127:0]                   currentPacket;    // The packet being transmitted

   reg [2:0]                     sync_cdc;         // cdc/edge detecting sync
   reg [26:0]                    syncCount;        // Stretcher for sync

   reg [24:0]                    syncIndStretch;   // Sync indication stretch
   
   reg [4:0]                     count;            // Which element we are outputting?
   reg [6:0]                     senderState;      // Onehot state encoding for uploader

   reg [2:0]                     pkavail_cdc;      // cdc/edge for packet finder
   wire                          packetWrite;      // Flag to write packet to memory

   /* Memory for storage of samples, one packet wide */
   ram #(.addr_width(BUFFLENLOG2),.data_width(128))
   mem (
            .din(Packet),
            .write_en(packetWrite),
            .waddr(Wp),
            .wclk(clk),

            .raddr(Rp),
            .rclk(clk),
            .dout(ramData)
        );

   assign packetWrite = ((pkavail_cdc==3'b011) || (pkavail_cdc==3'b100));
   wire syncStretch = (syncCount!=0);
   wire syncInd = (syncIndStretch!=0);
   
   /* States for uploader state machine */
   parameter
     ST_IDLE           =0,
     ST_SEND_SYN       =1,
     ST_SEND_DAT       =2,
     ST_LOAD_TX_FRAME  =3,
     ST_SENDING_FRAME  =4;

   // ======= Write data to RAM using source domain clock ===============================

   always @(posedge clk,posedge rst)
     begin
        DataOverf<=0;           // Each iteration, default is no overflow
        DataReady<=1'b0;        // ...and no data waiting to send

	if (rst)  // Perform reset actions ==============================================
	  begin
	     Wp<=0;
             nextWp<=1;
             senderState<=(1<<ST_IDLE);
             Rp<=0;
	  end
	else
          
	  begin // Perform new packet received actions ==================================

             // Perform cdc activities
             sync_cdc<={sync_cdc[1:0],sync};
             pkavail_cdc<={pkavail_cdc[1:0],PkAvail};

             // Check for sync
             if (syncCount!=0) syncCount<=syncCount-1;
             if (syncIndStretch!=0) syncIndStretch<=syncIndStretch-1;
             if ((sync_cdc==3'b011) || (sync_cdc==3'b100)) syncCount<=~0;

             // Store packet if we can
             if ((packetWrite) && (syncStretch))
               begin
                  // We have a new packet, we will store it and check overflow
                  $display("New packet: %32x",Packet);
                  syncIndStretch<=~0;
                  if (nextWp==Rp)
                    begin
                       Rp<=Rp+1;
                       DataOverf<=1'b1;
                       $display("Overflowed");
                    end
                  Wp<=nextWp;
                  nextWp<=nextWp+1;
               end

             // ======== Send data to uart if relevant ============

             case (senderState)
               (1<<ST_IDLE): /* Waiting for some data to become available ============= */
                 begin
                    count<=5'h04;
                    if (Rp!=Wp)
                      begin
                         senderState<=(1<<ST_SEND_SYN);
                      end
                 end

               (1<<ST_SEND_SYN): /* Sending components of sync ======================== */
                 begin
                    if (DataNext)
                      begin
                         if (count==0)
                           senderState<=(1<<ST_LOAD_TX_FRAME);
                         else
                           begin
                              count<=count-1;
                              DataVal<=(count==1)?8'h7f:8'hff;
                              DataReady<=1'b1;
                           end
                      end // if (DataNext)
                 end // case: (1<<ST_SEND_SYN)

               (1<<ST_LOAD_TX_FRAME): /* Loading frame to be sent ====================== */
                 begin
                    count<=5'h10;
                    currentPacket<=ramData;
                    senderState<=(1<<ST_SENDING_FRAME);
                 end

               (1<<ST_SENDING_FRAME): /* Outputting the frame ========================== */
                 begin
                    if (count==0)
                      begin
                         Rp<=Rp+1;
                         senderState<=(1<<ST_IDLE);
                      end
                    else
                      begin
                         if (DataNext)
                           begin
                              DataReady<=1'b1;
                              DataVal<=currentPacket[7:0];
                              count<=count-1;
                              currentPacket<={8'h0,currentPacket[127:8]};
                           end // if (DataNext)
                      end // case: senderState[ST_SENDING_PACKET]
                 end // case: (1<<ST_SENDING_FRAME)
             endcase // case (senderState)
          end // else: !if(rst)
     end // always @ (clk,posedge rst)
endmodule // packBuild
