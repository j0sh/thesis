#!/bin/bash

function r {
prefix="Grains/saliency/cvpr07supp"
imgp="$prefix/in"
spec="$prefix/sMap"
itti="$prefix/iMap"
ftsrd="$prefix/results-ftsrd2"
imgs=`cd $imgp;ls *.jpg`
resp="$prefix/results"
out="$prefix/combined"
for f in $imgs
do
    i=`echo $f | grep -Eo "^\w+"`
    ./tile.sh $imgp/$f $resp/$f $itti/$f $spec/$f $ftsrd/$f $out/$f
done
}

function g {
prefix="Grains/saliency/learning/achanta-dataset"
imgp="$prefix/images"
spec="$prefix/results-smap"
itti="$prefix/results-imap"
ftsrd="$prefix/results-ftsrd2"
imgs=`cd $imgp;ls *.jpg`
resp="$prefix/results-ours"
out="$prefix/combined"
for f in $imgs
do
    echo $f | grep -Eo "^\w+"
    ./tile.sh $imgp/$f $resp/$f $itti/$f $spec/$f $ftsrd/$f $out/$f
done
}

g
