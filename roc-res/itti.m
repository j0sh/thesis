function itti()
addpath(genpath('/home/josh/Desktop/simpsal'));
prefix='/media/Grains/saliency/learning/achanta-dataset/';
dirp=strcat(prefix, 'images/*.jpg');
files=dir(dirp);
for j = 1:length(files)
    fname = files(j).name;
    disp(strcat('Processing ', fname));
    img = imread(strcat(prefix, 'images/', fname));
    map = simpsal(img);
    bigmap = mat2gray(imresize(map, [size(img, 1) size(img, 2)]));
    imwrite(bigmap, strcat(prefix, 'results-imap/', fname), 'jpg');
end
