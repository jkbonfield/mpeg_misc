#!/bin/sh
in=$1;out=$2

rans=~/work/compression/rans_static/r4x16b
rle=../comp/rle2

mkdir $out 2>/dev/null

for f in $in.[0-9]*
do
    base=$(echo $f | sed 's:.*/::')

    r0=$($rans -o0 $f 2>/dev/null | wc -c)
    r1=$($rans -o1 $f 2>/dev/null | wc -c)
    r0R=$($rle < $f | $rans -o0 2>/dev/null | wc -c)
    r1R=$($rle < $f | $rans -o1 2>/dev/null | wc -c)
    #gz=$(gzip -9 < $f | wc -c)
    #bz=$(bzip2 -9 < $f | wc -c)
    #xz=$(xz -9 < $f | wc -c)
    
    min=99999999
    
    if [ $r0  -lt $min ]; then min=$r0;  method=r0;  fi
    if [ $r1  -lt $min ]; then min=$r1;  method=r1;  fi
    if [ $r0R -lt $min ]; then min=$r0R; method=r0R; fi
    if [ $r1R -lt $min ]; then min=$r1R; method=r1R; fi
    #if [ $gz -lt $min ]; then min=$gz; method=gz; fi
    #if [ $bz -lt $min ]; then min=$bz; method=bz; fi
    #if [ $xz -lt $min ]; then min=$xz; method=xz; fi
    
    #echo $method $min $r0,$r1,$r0R,$r1R,$gz,$bz,$xz $f,$base
    
    case $method in
    r0)  $rans -o0 < $f > $out/$base.r0 2>/dev/null;;
    r1)  $rans -o1 < $f > $out/$base.r1 2>/dev/null;;
    r0R) $rle < $f | $rans -o0 > $out/$base.r0R 2>/dev/null;;
    r1R) $rle < $f | $rans -o1 > $out/$base.r1R 2>/dev/null;;
    #gz) gzip -9 < $f > $out/$base.gz;;
    #bz) bzip2 -9 < $f > $out/$base.gz;;
    #xz) xz -9 < $f > $out/$base.gz;;
    *) echo FAIL; exit 1;;
    esac
done

cat $out/* | wc -c

