function thresh()
prefix = '../Grains/saliency/learning/achanta-dataset/';
files = dir(strcat(prefix, 'results-ours-0.00'));
masks = dir(strcat(prefix, 'binarymasks'));

figure;
hold;

function plotkde(fname, mname)
mask = imread(strcat(prefix, 'binarymasks/', mname));
mask = im2double(mask(:, :, 1));
im = im2double(imread(strcat(prefix, 'results-ours-0.00/', fname)));

% for some reason sometimes sizes differ, so truncate to smaller
minsz = min(size(mask), size(im));
mask = mask(1:minsz(1), 1:minsz(2));
im = im(1:minsz(1), 1:minsz(2));

res = mask .* im;
hg = imhist(res);
hg(1:10) = 0;
kde = ksdensity(hg);
kde(1:10) = 0;
plot(kde);
end

for k = 1:10 %length(files)
    if (~files(k).isdir)
        %disp(files(k).name);
        plotkde(files(k).name, masks(k).name);
    end
end

end
