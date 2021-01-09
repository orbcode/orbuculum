#!/usr/bin/python3

# This hack will convert a csv file into a straight datafile for
# pumping into a test deck. The csv contains two columns; a sample
# number and a 5 bit hex value representing the state of the four
# trace pins and the clock pin.  The sample number is ignored and
# the data is convered herein.

binval=0
ainval=0
with open("slowitm.csv", "r") as infile:
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
