#!/bin/bash
(
echo "ucinewgame"
echo "setoption name OwnBook value false"
echo "go movetime 2000"
sleep 2
echo "quit"
) | ./donut
