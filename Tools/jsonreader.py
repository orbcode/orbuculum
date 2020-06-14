#!/usr/bin/python3
import json

outputTable={}
historylen=10

with open('s5.json') as f:
    for intervaldata in f:
        jsondata = json.loads(intervaldata)

        for t in jsondata["toptable"]:
            if t["function"] not in outputTable:
                outputTable[t["function"]]={"visited":True,"history":[0]*historylen}
            outputTable[t["function"]]["history"]=outputTable[t["function"]]["history"][1:]+[t["count"]]
            outputTable[t["function"]]["history"]=outputTable[t["function"]]["history"][1:]+[t["count"]]

        for v,t in outputTable:
            print(t["visited"])
