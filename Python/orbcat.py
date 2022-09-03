from pyorb import *

o = Orb()
while (True):
    p = o.rx()

    match p.__class__.__name__:
        case "swMsg":
            print(''.join(chr((p.value >> (n * 8)) & 0xff) for n in range(p.len)), end = '')
            
        case other:
            continue
