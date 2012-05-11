function[iimg] = denoise(theimg)
s = 8; % block and transform size
ss = [s s];
dctim = blockproc(theimg, ss, @(bs)dct2(bs.data), 'PadPartialBlocks', true);
q = ones(s, s);
q(1,1) = 0; % ignore DC value

% original method by selecting max AC
i2 = blockproc(dctim, ss, @(bs)max(max(bs.data .* q)));

% test using Sobel edge detection
%i2 = imfilter(theimg, fspecial('sobel'));

% test using Difference of Gaussians
%i2 = imfilter(theimg, fspecial('gaussian', 10, 5)) - imfilter(theimg, fspecial('gaussian', 2, 1));

q = ones(s, s) - 1;
q(1, 1) = 1; % ignore AC values

% dilate edges
se = strel('disk', 2);
maxac = imdilate(i2, se);

% threshold
binimg = im2bw(maxac, graythresh(maxac));

%ifun = @(bs)idct2(bs.data * binimg(ceil(bs.location(1)/8), ceil(bs.location(2)/8)) + (q .* dctim(bs.location(1), bs.location(2)))); % retain DC
ifun = @(bs)idct2(bs.data * binimg(ceil(bs.location(1)/s), ceil(bs.location(2)/s)));

iimg = blockproc(dctim, ss, ifun); % run per block

iimg = binimg .* theimg; % run per pixel
end
