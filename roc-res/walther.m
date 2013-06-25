function walther()
% saliency using walther and koch
addpath(genpath('/home/josh/Desktop/SaliencyToolbox'));
prefix='/media/Grains/saliency/learning/achanta-dataset/';
p=strcat(prefix, 'images');
salmaps = batchSaliency(p);
files=dir(strcat(p, '/*.jpg'));
for j = 1:length(salmaps)
    fname = files(j).name;
    disp(strcat('Processing ', fname));
    img = imread(strcat(prefix, 'images/', fname));
    bigmap = mat2gray(imresize(salmaps(j).data, [size(img, 1) size(img, 2)]));
    imwrite(bigmap, strcat(prefix, 'results-kmap/', fname), 'jpg');
end
