#!/bin/bash

our_files=`ls dataset1/ours_*`
ftsrd_files=`ls dataset1/ftsrd_*`
smap_files=`ls dataset1/smap_*`
imap_files=`ls dataset1/imap_*`

function precision {
count=$#
for i in $(seq 2 255);
do
    bar=0
    for g in $@
    do
        foo=`sed -n "${i}p" $g | grep -Eo "precision [0-9].[0-9]+|precision -?nan" | grep -Eo "[0-9].[0-9]+|-?nan"`
        bar=`echo "$foo + $bar" | bc`
    done
    echo "$bar / $count" | bc -l
done

}

function recall {
count=$#
for i in $(seq 2 255);
do
    bar=0
    for g in $@
    do
        foo=`sed -n "${i}p" $g | grep -Eo "recall [0-9].[0-9]+|recall -?nan" | grep -Eo "[0-9].[0-9]+|-?nan"`
        bar=`echo "$foo + $bar" | bc`
    done
    echo "$bar / $count" | bc -l
done
}

function fpr {
count=$#
for i in $(seq 2 255);
do
    bar=0
    for g in $@
    do
        foo=`sed -n "${i}p" $g | grep -Eo "fpr [0-9].[0-9]+|fpr -?nan" | grep -Eo "[0-9].[0-9]+|-?nan"`
        bar=`echo "$foo + $bar" | bc`
    done
    echo "$bar / $count" | bc -l
done
}

#recall $our_files | tee dataset1/ours.recall
#recall $ftsrd_files |tee dataset1/ftsrd.recall
#recall $smap_files | tee dataset1/smap.recall
#recall $imap_files | tee dataset1/imap.recall
#precision $our_files | tee dataset1/ours.precision
#precision $ftsrd_files | tee dataset1/ftsrd.precision
#precision $smap_files | tee dataset1/smap.precision
#precision $imap_files | tee dataset1/imap.precision
#fpr $our_files | tee dataset1/ours.fpr
#fpr $ftsrd_files | tee dataset1/ftsrd.fpr
#fpr $smap_files | tee dataset1/smap.fpr
#fpr $imap_files | tee dataset1/imap.fpr

#imap_files2=`ls dataset2/imap_*`
#precision $imap_files2 | tee dataset2/imap.precision
#recall $imap_files2 | tee dataset2/imap.recall
#fpr $imap_files2 | tee dataset2/imap.fpr

#kmap_files2=`ls dataset2/kmap_*`
#precision $kmap_files2 | tee dataset2/kmap.precision
#recall $kmap_files2 | tee dataset2/kmap.recall
#fpr $kmap_files2 | tee dataset2/kmap.fpr

#smap_files2=`ls dataset2/smap_*`
#precision $smap_files2 | tee dataset2/smap.precision
#recall $smap_files2 | tee dataset2/smap.recall
#fpr $smap_files2 | tee dataset2/smap.fpr

our_files10=`ls dataset-1.00/ours_*`
#precision $our_files10 | tee dataset-0.10/ours.precision
recall $our_files10 | tee dataset-1.00/ours.recall
fpr $our_files10 | tee dataset-1.00/ours.fpr

our_files75=`ls dataset-0.75/ours_*`
#precision $our_files75 | tee dataset-0.75/ours.precision
recall $our_files75 | tee dataset-0.75/ours.recall
fpr $our_files75 | tee dataset-0.75/ours.fpr

#our_files25=`ls dataset-0.25/ours_*`
#precision $our_files25 | tee dataset-0.25/ours.precision
#recall $our_files25 | tee dataset-0.25/ours.recall
#fpr $our_files25 | tee dataset-0.25/ours.fpr

#our_files2=`ls dataset2/ours_*`
#precision $our_files2 | tee dataset2/ours.precision
#recall $our_files2 | tee dataset2/ours.recall
#fpr $our_files2 | tee dataset2/ours.fpr

#ftsrd_files2=`ls dataset2/ftsrd_*`
#precision $ftsrd_files2 | tee dataset2/ftsrd.precision
#recall $ftsrd_files2 | tee dataset2/ftsrd.recall
#fpr $ftsrd_files2 | tee dataset2/ftsrd.fpr
