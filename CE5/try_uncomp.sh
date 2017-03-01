#!/bin/sh
in=$1;out=$2

mkdir $out 2>/dev/null

rans=~/work/compression/rans_static/r4x16b
rle=../comp/rle2

for i in $in/*
do
  case $i in
      *.cat) cat $i > $(echo $i | sed "s:.*/\(.*\)\.cat$:$out/\1:");;
      *.r0)  $rans -d < $i > $(echo $i | sed "s:.*/\(.*\)\.r0$:$out/\1:");;
      *.r1)  $rans -d < $i > $(echo $i | sed "s:.*/\(.*\)\.r1$:$out/\1:");;
      *.r0R) $rans -d < $i | $rle -d > $(echo $i | sed "s:.*/\(.*\)\.r0R$:$out/\1:");;
      *.r1R) $rans -d < $i | $rle -d > $(echo $i | sed "s:.*/\(.*\)\.r1R$:$out/\1:");;
      *) echo FAIL; exit 1;;
  esac
done
