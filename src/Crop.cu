#include "cuda.h"
#include "VideoProcessor.h"

__global__ void cropKernel(unsigned char* inputY, unsigned char* inputUV, unsigned char* outputY, unsigned char* outputUV,
	int srcLinesizeY, int srcLinesizeUV, int topLeftX, int topLeftY, int botRightX, int botRightY) {
	unsigned int i = blockIdx.y * blockDim.y + threadIdx.y; //coordinate of pixel (y) in destination image
	unsigned int j = blockIdx.x * blockDim.x + threadIdx.x; //coordinate of pixel (x) in destination image
	if (i < botRightX - topLeftX && j < botRightY - topLeftY) {
		int UVRow = i / 2;
		int UVCol = j % 2 == 0 ? j : j - 1;
		int UIndexSrc = (topLeftY / 2 + UVRow) * srcLinesizeUV /*pitch?*/ + (UVCol + topLeftX);
		int VIndexSrc = (topLeftY / 2 + UVRow) * srcLinesizeUV /*pitch?*/ + (UVCol + topLeftX + 1);

		int UIndexDst = UVRow * (botRightX - topLeftX) /*pitch?*/ + UVCol;
		int VIndexDst = UVRow * (botRightX - topLeftX) /*pitch?*/ + UVCol + 1;

		outputY[j + i * (botRightX - topLeftX)] = inputY[(topLeftX + j) + (topLeftY + i) * srcLinesizeY];
		outputUV[UIndexDst] = inputUV[UIndexSrc];
		outputUV[VIndexDst] = inputUV[VIndexSrc];
	}
}

int cropHost(AVFrame* src, AVFrame* dst, CropOptions crop, int maxThreadsPerBlock, cudaStream_t * stream) {
	cudaError err;
	unsigned char* outputY = nullptr;
	unsigned char* outputUV = nullptr;
	err = cudaMalloc(&outputY, dst->width * dst->height * sizeof(unsigned char)); //in resize we don't change color format
	err = cudaMalloc(&outputUV, dst->width * (dst->height / 2) * sizeof(unsigned char));
	//need to execute for width and height
	dim3 threadsPerBlock(64, maxThreadsPerBlock / 64);
	int blockX = std::ceil(dst->width / (float)threadsPerBlock.x);
	int blockY = std::ceil(dst->height / (float)threadsPerBlock.y);
	dim3 numBlocks(blockX, blockY);

	cropKernel << <numBlocks, threadsPerBlock, 0, *stream >> > (src->data[0], src->data[1], outputY, outputUV,
		src->linesize[0], src->linesize[1], std::get<0>(crop.leftTopCorner), std::get<1>(crop.leftTopCorner),
											std::get<0>(crop.rightBottomCorner), std::get<1>(crop.rightBottomCorner));


/*	if (dst->data[0])
		err = cudaFree(dst->data[0]);
	if (dst->data[1])
		err = cudaFree(dst->data[1]);
*/
	dst->data[0] = outputY;
	dst->data[1] = outputUV;

	return err;
}