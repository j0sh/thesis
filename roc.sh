function dataset1 {
    prefix="/media/Grains/saliency/cvpr07supp"
    imgp="$prefix/hMap/a1"
    salp="$prefix/results-ftsrd"
    imgs=`cd $imgp;ls *.jpg`
    resp="$prefix/results"
    smap="$prefix/sMap"
    imap="$prefix/iMap"
    for f in $imgs
    do
        i=`echo $f | grep -Eo "^\w+"`
        #echo "ours $f"
        #./a.out $imgp/$f $resp/$f > roc-res/dataset1/ours_$f.out
        #echo "ftsrd $f"
        #./a.out $imgp/$f $salp/$f > roc-res/dataset1/ftsrd_$f.out
        #echo "sMap $f"
        #./a.out $imgp/$f $smap/$f > roc-res/dataset1/smap_$f.out
        echo "iMap $f"
        ./a.out $imgp/$f $imap/$f > roc-res/dataset1/imap_$f.out
    done
}

function dataset2 {
    prefix="/media/Grains/saliency/learning/achanta-dataset"
    maskp="$prefix/binarymasks"
    ftsrdp="$prefix/results-ftsrd"
    imgs=`cd $maskp;ls *.bmp`
    resp="$prefix/results-ours"
    ours25="$prefix/results-ours-0.25"
    ours75="$prefix/results-ours-0.75"
    ours10="$prefix/results-ours-1.00"
    smap="$prefix/results-smap"
    imap="$prefix/results-imap"
    kmap="$prefix/results-kmap"

    imgs=`echo $imgs| sed 's/.bmp//g'`

    for f in $imgs
    do
        i=`echo $f | grep -Eo "^\w+"`
        #echo "ours-1.00 $f"
        #./a.out $maskp/$f.bmp $ours10/$f.jpg > roc-res/dataset-1.00/ours_$f.out
        echo "ours-0.75 $f"
        ./a.out $maskp/$f.bmp $ours75/$f.jpg > roc-res/dataset-0.75/ours_$f.out
        #echo "ours-0.25 $f"
        #./a.out $maskp/$f.bmp $ours25/$f.jpg > roc-res/dataset-0.25/ours_$f.out
        #echo "ours $f"
        #./a.out $maskp/$f.bmp $resp/$f.jpg > roc-res/dataset2/ours_$f.out
        #echo "ftsrd $f"
        #./a.out $maskp/$f.bmp $ftsrdp/$f.jpg > roc-res/dataset2/ftsrd_$f.out
        #echo "sMap $f"
        #./a.out $maskp/$f.bmp $smap/$f.jpg > roc-res/dataset2/smap_$f.out
        #echo "kMap $f"
        #./a.out $maskp/$f.bmp $kmap/$f.jpg > roc-res/dataset2/kmap_$f.out
        #echo "iMap $f"
        #./a.out $maskp/$f.bmp $imap/$f.jpg > roc-res/dataset2/imap_$f.out
    done
}


#dataset1
dataset2
#`cd roc-res;./process.sh`
