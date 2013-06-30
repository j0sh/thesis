inputs="-i $1 -i $2 -i $3 -i $4 -i $5"

FILT=$(cat<<EOF
[0:v:0]scale=320:240[i1];
[1:v:0]scale=320:240[i2];
[2:v:0]scale=320:240[i3];
[3:v:0]scale=320:240[i4];
[4:v:0]scale=320:240[i5];
color=red:1600x240[base];
[base][i1]overlay=x=0:y=0[out1],
[out1][i2]overlay=x=320:y=0[out2],
[out2][i3]overlay=x=640:y=0[out3],
[out3][i4]overlay=x=960:y=0[out4],
[out4][i5]overlay=x=1280:y=0[out]
EOF
)

avconv -v error  $inputs -filter_complex "$FILT" -frames 1 -map [out] $6
