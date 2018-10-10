#!/bin/bash
while true; do dd if=/dev/zero of=/dev/null bs=1024000 count=1024; done & 
while true; do pkill hackbench; sleep 5; done&
while true; do  /home/debian/test/rt-tests/hackbench 20; done &
while true; do ping -l 100000 -q -s 10 -f localhost ; done &
while true; do du / > /dev/null  ; done & 
