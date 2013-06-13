path="/media/Grains/changedetection/"
dpath = path + "dataset/"
rpath = path + "results/"
name = ARGV.shift
fname = dpath + name
start,stop = IO.read(fname+"/temporalROI.txt").split(" ")
exec("../a.out #{fname}/input #{start} #{stop}")
