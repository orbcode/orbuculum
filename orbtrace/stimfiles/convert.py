#!/usr/bin/python3
binval=0
ainval=0
with open("fastitm.csv", "r") as infile:
    infile.readline()
    while True:
        ainval=binval
        while ((ainval&16)==(binval&16)):
            l1=infile.readline()
            if not l1:
                break
            ainval=int(l1.split(",")[1],16)
        if not l1:
            break
        
        # Spin forward until clock changes
        binval=ainval
        while ((binval&16)==(ainval&16)):
            l1=infile.readline()
            if not l1:
                break
            binval=int(l1.split(",")[1],16)

        print(((ainval&0x0f)<<4)|(binval&0x0f))
