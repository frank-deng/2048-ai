#!/usr/bin/env python3

import sys, csv;

if __name__ == '__main__':
    if len(sys.argv)<2:
        sys.stderr.write('Usage: %s logfile'%sys.argv[0]);
        exit(1);
    logfile = sys.argv[1];

    data = {};
    result = [];
    try:
        with open(logfile, 'r') as f:
            for row in csv.reader(f):
                if (len(row) != 4):
                    continue;
                key = str(row[2]);
                if (data.get(key)):
                    data[key] += 1;
                else:
                    data[key] = 1;
    except FileNotFoundError:
        pass;
    if len(sys.argv) >= 3 and sys.argv[2] == 'BASIC':
        print('100 DATA %d,5'%(len(data.keys())));
        i = 1;
        for k in sorted([int(n) for n in data.keys()]):
            print('%d DATA "%5d",%d'%(100+i,k,data[str(k)]));
            i += 1;
    else:
        for k in sorted([int(n) for n in data.keys()]):
            print(str(k)+','+str(data[str(k)]));
    exit(0);

