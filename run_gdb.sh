#!/bin/bash
(
echo "run"
echo "ucinewgame"
echo "setoption name OwnBook value false"
echo "go movetime 2000"
sleep 3
echo "bt"
echo "info locals"
echo "quit"
) | gdb -q ./donut > /tmp/gdb_output.txt
