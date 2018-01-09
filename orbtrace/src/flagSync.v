// Code from http://www.fpga4fun.com/CrossClockDomain2.html

module Signal_CrossDomain(
    input SignalIn_clkA,
    input clkB,
    output SignalOut_clkB
);

reg [1:0] SyncA_clkB;
always @(posedge clkB) SyncA_clkB[0] <= SignalIn_clkA;
always @(posedge clkB) SyncA_clkB[1] <= SyncA_clkB[0];

assign SignalOut_clkB = SyncA_clkB[1]; 
endmodule

module FlagAck_CrossDomain(
    input clkA,
    input FlagIn_clkA,
    output Busy_clkA,
    input clkB,
    output FlagOut_clkB
);

reg FlagToggle_clkA;
always @(posedge clkA) FlagToggle_clkA <= FlagToggle_clkA ^ (FlagIn_clkA & ~Busy_clkA);

reg [2:0] SyncA_clkB;
always @(posedge clkB) SyncA_clkB <= {SyncA_clkB[1:0], FlagToggle_clkA};

reg [1:0] SyncB_clkA;
always @(posedge clkA) SyncB_clkA <= {SyncB_clkA[0], SyncA_clkB[2]};

assign FlagOut_clkB = (SyncA_clkB[2] ^ SyncA_clkB[1]);
assign Busy_clkA = FlagToggle_clkA ^ SyncB_clkA[1];
endmodule // FlagAck_CrossDomain

module TaskAck_CrossDomain(
    input clkA,
    input TaskStart_clkA,
    output TaskBusy_clkA, TaskDone_clkA,

    input clkB,
    output TaskStart_clkB, TaskBusy_clkB,
    input TaskDone_clkB
);

reg FlagToggle_clkA, FlagToggle_clkB, Busyhold_clkB;
reg [2:0] SyncA_clkB, SyncB_clkA;

always @(posedge clkA) FlagToggle_clkA <= FlagToggle_clkA ^ (TaskStart_clkA & ~TaskBusy_clkA);

always @(posedge clkB) SyncA_clkB <= {SyncA_clkB[1:0], FlagToggle_clkA};
assign TaskStart_clkB = (SyncA_clkB[2] ^ SyncA_clkB[1]);
assign TaskBusy_clkB = TaskStart_clkB | Busyhold_clkB;
always @(posedge clkB) Busyhold_clkB <= ~TaskDone_clkB & TaskBusy_clkB;
always @(posedge clkB) if(TaskBusy_clkB & TaskDone_clkB) FlagToggle_clkB <= FlagToggle_clkA;

always @(posedge clkA) SyncB_clkA <= {SyncB_clkA[1:0], FlagToggle_clkB};
assign TaskBusy_clkA = FlagToggle_clkA ^ SyncB_clkA[2];
assign TaskDone_clkA = SyncB_clkA[2] ^ SyncB_clkA[1];
endmodule // TaskAck_CrossDomain
