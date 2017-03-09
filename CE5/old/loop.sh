#!/bin/sh

in=$1

tok=./tokenise_name2

rm -rf $in.*
$tok $in >/dev/null
for blk in $in.blk_??????.0_0
do
    b=`echo $blk | sed 's/\.0_0$//'`
    ./try_comp.sh $b $b.comp > /dev/null
    ./pack_dir $b.comp $b > $b.comp.pack
done
cat $in.blk_??????.comp.pack | wc -c
(for blk in $in.blk_??????.comp.pack
do
    b=`echo $blk | sed 's/\.comp.pack//'`
    ./unpack_dir $b.unpack $b < $b.comp.pack
    ./try_uncomp.sh $b.unpack $b.uncomp 2>/dev/null
    $tok -d $b.uncomp/$b
done) > $in.new
cmp $in $in.new



