#!/bin/sh
in=$1;out=$2;no_recurse=$3

rans=~/work/compression/rans_static/r4x16b
rle=../comp/rle2

mkdir $out 2>/dev/null

#no_recurse=1

# FIXME: make rx4 do r0 and r1 and pick whichever is best.
comp() {
    f=$1
    base=$(echo $f | sed 's:.*/::')

    min=99999999

    cat=$(cat $f | wc -c)
    RLE=$($rle < $f | wc -c)
    r0=$($rans -o0 $f 2>/dev/null | wc -c)
    r1=$($rans -o1 $f 2>/dev/null | wc -c)
    r0R=$($rle < $f | $rans -o0 2>/dev/null | wc -c)
    r1R=$($rle < $f | $rans -o1 2>/dev/null | wc -c)

    # ix4 is SLOW.
    # It splits a file in 4 and then recurses into try_comp.sh again to try
    # all the methods to compress it with.
    if [ "$no_recurse" == "" ]
    then
	ix4=$(./ix4_comp.sh $f | wc -c)
    else
	ix4=$min
    fi
    #r0x4=$((for i in 1 2 3 4;do ./nth $i 4 < $f | $rans -o0 2>/dev/null;done) | wc -c)
    #gz=$(gzip -9 < $f | wc -c)
    #bz=$(bzip2 -9 < $f | wc -c)
    #xz=$(xz -9 < $f | wc -c)
   
    if [ $cat -lt $min ]; then min=$cat; method=cat; fi
    if [ $RLE -lt $min ]; then min=$RLE; method=rle; fi
    if [ $r0  -lt $min ]; then min=$r0;  method=r0;  fi
    if [ $r1  -lt $min ]; then min=$r1;  method=r1;  fi
    if [ $r0R -lt $min ]; then min=$r0R; method=r0R; fi
    if [ $r1R -lt $min ]; then min=$r1R; method=r1R; fi
    if [ $ix4 -lt $min ]; then min=$ix4; method=ix4; fi
    #if [ $r0x4 -lt $min ]; then min=$r0x4;  method=r0x4; fi
    #if [ $gz -lt $min ]; then min=$gz; method=gz; fi
    #if [ $bz -lt $min ]; then min=$bz; method=bz; fi
    #if [ $xz -lt $min ]; then min=$xz; method=xz; fi
    
    #echo $method $min $r0,$r1,$r0R,$r1R,$gz,$bz,$xz $f,$base
    
    case $method in
    cat) cat $f > $out/$base.cat;;
    rle) $rle < $f > $out/$base.rle;;
    r0)  $rans -o0 < $f > $out/$base.r0 2>/dev/null;;
    r1)  $rans -o1 < $f > $out/$base.r1 2>/dev/null;;
    r0R) $rle < $f | $rans -o0 > $out/$base.r0R 2>/dev/null;;
    r1R) $rle < $f | $rans -o1 > $out/$base.r1R 2>/dev/null;;
    ix4) ./ix4_comp.sh $f > $out/$base.ix4 2>/dev/null;;
    #r0x4) (for i in 1 2 3 4;do ./nth $i 4 < $f | $rans -o0 2>/dev/null;done) > $out/$base.r0x4;;
    #gz) gzip -9 < $f > $out/$base.gz;;
    #bz) bzip2 -9 < $f > $out/$base.gz;;
    #xz) xz -9 < $f > $out/$base.gz;;
    *) echo FAIL; exit 1;;
    esac
}

for f in $in.[0-9]*
do
    comp $f
done

expr `/bin/ls -1 $out/*|wc -l` \* 2 + `cat $out/* | wc -c`
