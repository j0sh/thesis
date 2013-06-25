function xhou()
% saliency using spectral residual
prefix='/media/Grains/saliency/learning/achanta-dataset/';
dirp=strcat(prefix, 'images/*.jpg');
files=dir(dirp);
for j = 1:length(files)
    fname = files(j).name;
    disp(strcat('Processing ', fname));
    img = spectralresidual(strcat(prefix, '/images/', fname));
    imwrite(img, strcat(prefix, 'results-smap/', fname), 'jpg');
end
