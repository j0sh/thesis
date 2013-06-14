#!/bin/bash

function runsal {
    imgs=`cd $1;ls *.jpg`
    for f in $imgs
    do
        ./a.out $1/$f $2/$f
    done
}

function r {
    prefix="/media/Grains/saliency/learning/Image"
    ./a.out $prefix/$1
}

function g {
r "6/6_186_186022.jpg" # bird on fence
r "1/1_45_45397.jpg" # leaf on fence
r "8/9_210088.jpg" # fish
r "8/9_65019.jpg"  # chinese man
r "8/9_388016.jpg" # leaning girl
r "6/6_185_185184.jpg" # bird on fence 2
r "1/1_30_30895.jpg" # gnome
r "0/0_9_9990.jpg" # saltshakers
r "0/0_24_24209.jpg" # 440
}

# from "saliency detection: a spectral residual approach", x. hou etal
#runsal "/media/Grains/saliency/cvpr07supp/in" "/media/Grains/saliency/cvpr07supp/results"

g
