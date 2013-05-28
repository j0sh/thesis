foo=`echo $1 | grep -Eo '[0-9]+\.jpg' | grep -Eo '[0-9]+'`
bar=`echo $2 | grep -Eo '[0-9]+\.jpg' | grep -Eo '[0-9]+'`
output=/home/josh/Desktop/vidpairs/gt/$foo-$bar.png
./gt "$1" "$2" $output
