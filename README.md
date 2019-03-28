



# TensorStream README
TensorStream is a C++ library for real-time video stream (e.g. RTMP) decoding to CUDA memory which support some additional features:
* CUDA memory conversion to ATen Tensor for using it via Python in [Pytorch Deep Learning models](#pytorch-example)
* Detecting basic video stream issues related to frames reordering/loss
* VPP operations: downscaling/upscaling, color conversion from NV12 to RGB24/BGR24/Y800

The whole pipeline works on GPU.

# Table of Contents
 - [Binaries](#binaries)
 - [Installation](#installation-from-source)
 - [Usage](#usage)
 - [Documentation](#documentation)

## Binaries
Extension for Python can be installed via pip (**Linux only**):
 - **CUDA 9:**
```
pip install https://tensorstream.argus-ai.com/wheel/linux/tensor_stream-0.1.5-cp36-cp36m-linux_x86_64.whl
```
- **CUDA 10:**
```
TBD
```
Python 3.6 or above is required
## Installation from source
### Install dependencies
* [NVIDIA CUDA](https://developer.nvidia.com/cuda-downloads) 9.0 or above
* [FFmpeg](https://github.com/FFmpeg/FFmpeg) and FFmpeg version of headers required to interface with Nvidias codec APIs
[nv-codec-headers](https://github.com/FFmpeg/nv-codec-headers)
* [Pytorch](https://github.com/pytorch/pytorch) 1.0 to build C++ extension for Python
* [Python](https://www.python.org/) 3.6 or above to build C++ extension for Python

To build TensorStream on Windows, Visual Studio 2017 14.11 toolset is needed
### TensorStream source code

```
git clone -b master --single-branch https://github.com/Fonbet/argus-tensor-stream.git
cd argus-tensor-stream
```
### Install TensorStream
#### C++ extension for Python

On Linux:
```
python setup.py install
```
On Windows:
```
set FFMPEG_PATH="Path to FFmpeg install folder"
set path=%path%;%FFMPEG_PATH%\bin
set VS150COMNTOOLS="Path to Visual Studio vcvarsall.bat folder"
call "%VS150COMNTOOLS%\vcvarsall.bat" x64 -vcvars_ver=14.11
python setup.py install
```
#### C++ library:

On Linux:
```
mkdir build
cd build
cmake ..
```
On Windows:
```
set FFMPEG_PATH="Path to FFmpeg install folder"
mkdir build
cd build
cmake -G "Visual Studio 15 2017 Win64" -T v141,version=14.11 ..
```
### Docker image
Dockerfiles can be found in ```docker``` folder. Please note that for different CUDAs different Dockerfiles are required. To distinguish them name suffix is used, i.e. for **CUDA 9** Dockerfile name  is Dockerfile_**cu9**, for **CUDA 10** Dockerfile_**cu10** and so on. 
To build image, copy one of Dockerfiles to the top level of repository, remove CUDA specific suffix and execute:
```
docker build -t tensorstream .
```
Run with bash command line and follow C++ extension for Python [installation guide](#install-tensorstream)
```
nvidia-docker run -ti tensorstream bash
```
### Building examples and tests
Examples for Python and C++ can be found in ```c_examples``` and ```python_examples``` folders.  Tests for C++ can be found in ```tests ``` folder.
#### Python example 
Can be executed via Python after TensorStream [C++ extension for Python](#c-extension-for-python) installation.
```
cd python_examples
python sample.py
```
#### C++ example and unit tests
On Linux
```
cd c_examples or tests
mkdir build
cd build
cmake ..
```
On Windows
```
set FFMPEG_PATH="Path to FFmpeg install folder"
cd c_examples or tests
mkdir build
cd build
cmake -G "Visual Studio 15 2017 Win64" -T v141,version=14.11 ..
```
## Usage

### Sample
Python example demonstrates RTMP to Pytorch tensor conversion. Let's consider some usage scenarios:
> **Note:** You can pass **--help** to get list of all available options, their description and default values

* Convert RTMP bitstream to RGB24 Pytorch tensor and dump result to dump.yuv file: 
```
python sample.py -i rtmp://184.72.239.149/vod/mp4:bigbuckbunny_1500.mp4 -fc RGB24 -o dump.yuv
```
> **Warning:** Dumps significantly affect performance

* The same scenario with downscaling:
```
python sample.py -i rtmp://184.72.239.149/vod/mp4:bigbuckbunny_1500.mp4 -fc RGB24 -w 720 -h 480 -o dump.yuv
```

* Number of frames can be limited by -n option:
```
python sample.py -i rtmp://184.72.239.149/vod/mp4:bigbuckbunny_1500.mp4 -fc RGB24 -w 720 -h 480 -o dump.yuv -n 100
```
### Pytorch example

Simple example how to use TensorStream for Deep learning tasks (pseudo-code):

```
reader = TensorStreamConverter("path-to-video")
reader.initialize()
reader.start()
parameters = {
    'name': "RGB_reader",
    'delay': 0,
    'pixel_format': RGB24,
    'return_index': False,
    'width': width,
    'height': height,
}

while(need predictions):
    tensor = reader.read(**parameters)
    prediction = model.predict(tensor)
```
Initialize tensor stream with video file (e.g. local or network video) and start reading it in separate process. Get last frame from read part of stream and do prediction.
> **Note:** All tasks inside TensorStream processed on GPU, so output tensor also located on GPU.

## Documentation
Documentation for Python and C++ API can be found on the [site](https://tensorstream.argus-ai.com/)