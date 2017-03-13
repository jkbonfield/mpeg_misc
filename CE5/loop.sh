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
    rm $b.[0-9]*
done
sz=`cat $in.blk_??????.comp | wc -c`
n=`ls -1 $in.blk_??????.comp | wc -l`
echo "$sz in $n blocks"

# Unpack
(for blk in $in.blk_??????.comp
do
    b=`echo $blk | sed 's/\.comp$//'`
    $codec -d $in.tmp < $b.comp
    $tok -d $in.tmp
done) > $in.new

cmp $in $in.new

rm $in.tmp.*
rm $in.new
