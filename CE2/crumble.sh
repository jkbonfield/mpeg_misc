#!/bin/sh
crumble=/nfs/users/nfs_j/jkb/lustre/mpeg/tools/crumble

bam=$1
base=`echo $bam|sed 's/\.bam$//'`

run() {
    echo -----------------------------------------------------------------------------
    echo ${@+"$@"}
    eval time ${@+"$@"}
}

run $crumble -1   -T BI,BD,BQ $bam $base.crumble-1.bam
run $crumble -9   -T BI,BD,BQ $bam $base.crumble-9.bam
run $crumble -9p8 -T BI,BD,BQ $bam $base.crumble-9p8.bam
echo -----------------------------------------------------------------------------
