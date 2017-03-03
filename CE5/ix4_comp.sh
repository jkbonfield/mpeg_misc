#!/bin/sh

# Take a 32-bit integer file and split into 4 8-bit integer components
# in a subdirectory.  Then compress those and pack them to get a
# combined new file.
#
# Ideally we'd have a single general entropy encoder binary to do all
# this for us, but this is a quick prototype.

rans=~/work/compression/rans_static/r4x16b
rle=../comp/rle2

if [ "$1" = "-d" ]
then
shift
    #_.unpack/_.0_11.ix4 _.uncomp

    comp_dir=`mktemp -d`
    ./unpack_dir $comp_dir x < $1
    uncomp_dir=`mktemp -d`
    ./try_uncomp.sh $comp_dir $uncomp_dir
    
    fn="$2/`echo $1 | sed 's:.*/\(.*\)\.ix4$:\1:'`"
    cat $uncomp_dir/* | ./nth -d 4 > $fn
    rm -rf $comp_dir $uncomp_dir
else
    in=$1
    base=`echo $in|sed 's:.*/::'`
    out_dir=`mktemp -d`
    out=$out_dir/$base.dir
    mkdir $out
    for i in 1 2 3 4
    do
	./nth $i 4 < $in > $out/x.0_$i
    done

    comp_dir=`mktemp -d`
    ./try_comp.sh $out/x $comp_dir 1 > /dev/null
    ./pack_dir $comp_dir x
    rm -rf $out_dir $comp_dir
fi
