#!/bin/sh
in=$1;out=$2

mkdir $out 2>/dev/null

rans=~/work/compression/rans_static/r4x16b

for i in $in/*
do
  case $i in
      *.r0) $rans -d < $i > $(echo $i | sed "s:.*/\(.*\)\.r0$:$out/\1:");;
      *.r1) $rans -d < $i > $(echo $i | sed "s:.*/\(.*\)\.r1$:$out/\1:");;
      *) echo FAIL; exit 1;;
  esac
done
