#!/bin/sh

in=$1

tok=./tokenise_name2
codec=../comp/codec

# tokenise
rm -rf $in.*
$tok $in >/dev/null

# Pack
for blk in $in.blk_??????.000_00
do
    b=`echo $blk | sed 's/\.000_00$//'`
    $codec $b* > $b.comp
done
cat $in.blk_??????.comp | wc -c

# Unpack
(for blk in $in.blk_??????.comp
do
    b=`echo $blk | sed 's/\.comp$//'`
    $codec -d $in.tmp < $b.comp
    $tok -d $in.tmp
done) > $in.new

cmp $in $in.new



