#!/bin/sh

rm sched.py
rm traces 
cp /var/nfs/experiment-scripts-master/mrsp/sched.py >> sched.py
unit-trace /var/nfs/experiment-scripts-master/run-data/mrsp/st-* -o >> traces
java -jar mrsp.jar

