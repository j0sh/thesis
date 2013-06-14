#!/bin/bash

function runsal {
    for f in $1/*.jpg
    do
        ./a.out $f
    done
}

function r {
    prefix="/media/Grains/saliency/learning/Image"
    ./a.out $prefix/$1
}

# from "saliency detection: a spectral residual approach", x. hou etal
#runsal "/media/Grains/saliency/cvpr07supp/in"

r "6/6_186_186022.jpg" # bird on fence
r "1/1_45_45397.jpg" # leaf on fence
r "8/9_210088.jpg" # fish
r "8/9_65019.jpg"  # chinese man
r "8/9_388016.jpg" # leaning girl
r "6/6_185_185184.jpg" # bird on fence 2
r "1/1_30_30895.jpg" # gnome
r "0/0_9_9990.jpg" # saltshakers
r "0/0_24_24209.jpg" # 440
