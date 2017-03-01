#!/bin/sh

in=$1

rm -rf $in.*
./tokenise_name2 $in
./try_comp.sh $in $in.comp > /dev/null
./pack_dir $in.comp $in > $in.comp.pack
wc -c $in.comp.pack
./unpack_dir $in.unpack $in < $in.comp.pack
./try_uncomp.sh $in.unpack $in.uncomp 2>/dev/null
./a.out -d $in.uncomp/$in > $in.new
cmp $in $in.new



