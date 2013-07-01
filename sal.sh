#!/bin/bash

function runsal {
    imgs=`cd $1;ls *.jpg`
    for f in $imgs
    do
        ./a.out $1/$f $2/$f
    done
}

function runsal2 {
    imgs=`cd $1;ls *.jpg`
    for f in $imgs
    do
        ./a.out $1/$f
    done
}


function r {
    prefix="Grains/saliency/learning/Image"
    ./a.out $prefix/$1
}

# some commonly exhibited images
function g {
r "4/4_129_129095.jpg" # moses figurine
r "6/6_186_186022.jpg" # bird on fence
r "1/1_45_45397.jpg" # leaf on fence
r "8/9_210088.jpg" # fish
r "8/9_65019.jpg"  # chinese man
r "8/9_388016.jpg" # leaning girl
r "6/6_185_185184.jpg" # bird on fence 2
r "8/9_163014.jpg" # birdnest
r "1/1_30_30895.jpg" # gnome
r "0/0_9_9990.jpg" # saltshakers
r "7/7_197_197020.jpg" # phonebooth
r "0/0_24_24209.jpg" # 440
}

function h {
r "6/6_187_187262.jpg" # ducklings
r "6/6_186_186978.jpg" # sliding
r "6/6_186_186937.jpg" # bird
r "8/9_11_11346.jpg" # sign
r "8/9_108_0883.jpg" # cow
r "8/9_17215.jpg" # grapes
r "8/9_17216.jpg" # girls
r "8/9_300091.jpg" # surfer
r "8/9_299086.jpg" # camel
r "8/9_147091.jpg" # tree
r "9/10_75671582_152bf239ef.jpg" # daisies
r "9/10_236916255_307bc2272c.jpg" # horsey
r "6/6_187_187089.jpg" # shack
}

# from "saliency detection: a spectral residual approach", x. hou etal
#runsal "/media/Grains/saliency/learning/achanta-dataset/images" "/media/Grains/saliency/learning/achanta-dataset/results-ftsrd"
#runsal "Grains/saliency/learning/achanta-dataset/images" "Grains/saliency/learning/achanta-dataset/results-sbar"
#runsal "/media/Grains/saliency/cvpr07supp/in" "/media/Grains/saliency/cvpr07supp/results"
#runsal "/media/Grains/saliency/cvpr07supp/in" "/media/Grains/saliency/cvpr07supp/results-ftsrd"
runsal2 "Grains/saliency/cvpr07supp/in"
#runsal2 "/media/Grains/saliency/learning/Image/6"
#runsal2 "/media/Grains/saliency/learning/Image/7"

#r "0/0_9_9990.jpg" # saltshakers
#r "1/1_45_45397.jpg" # leaf on fence
#r "8/9_210088.jpg" # fish

#g

#h
