#!/bin/sh
in=$1;out=$2

rans=~/work/compression/rans_static/r4x16b

mkdir $out 2>/dev/null

for f in $in.[0-9]*
do
    r0=$($rans -o0 $f 2>/dev/null | wc -c)
    r1=$($rans -o1 $f 2>/dev/null | wc -c)
    #gz=$(gzip -9 < $f | wc -c)
    #bz=$(bzip2 -9 < $f | wc -c)
    #xz=$(xz -9 < $f | wc -c)
    
    min=99999999
    
    if [ $r0 -lt $min ]; then min=$r0; method=r0; fi
    if [ $r1 -lt $min ]; then min=$r1; method=r1; fi
    #if [ $gz -lt $min ]; then min=$gz; method=gz; fi
    #if [ $bz -lt $min ]; then min=$bz; method=bz; fi
    #if [ $xz -lt $min ]; then min=$xz; method=xz; fi
    
    #echo $method $min $r0,$r1,$gz,$bz,$xz $f
    
    case $method in
    r0) $rans -o0 < $f > $out/$f.r0 2>/dev/null;;
    r1) $rans -o1 < $f > $out/$f.r1 2>/dev/null;;
    #gz) gzip -9 < $f > $out/$f.gz;;
    #bz) bzip2 -9 < $f > $out/$f.gz;;
    #xz) xz -9 < $f > $out/$f.gz;;
    *) echo FAIL; exit 1;;
    esac
done

cat $out/* | wc -c

