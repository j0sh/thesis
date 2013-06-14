#!/bin/bash

function runsal {
    for f in $1/*.jpg
    do
        ./a.out $f
    done
}

# from "saliency detection: a spectral residual approach", x. hou etal
runsal "/media/Grains/saliency/cvpr07supp/in"
