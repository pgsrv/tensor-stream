#include <gtest/gtest.h>
#include <algorithm>

#ifdef WIN32
#define NOMINMAX
#include "Windows.h"
#include "psapi.h"
#endif

#include "WrapperC.h"
extern "C" {
#include "libavutil/crc.h"
}

void getCycle(std::map<std::string, std::string> parameters, TensorStream& reader) {
	try {
		int width = std::atoi(parameters["width"].c_str());
		int height = std::atoi(parameters["height"].c_str());
		FourCC format = (FourCC)std::atoi(parameters["format"].c_str());
		int frames = std::atoi(parameters["frames"].c_str());

		std::string fileName = parameters["dumpName"];
		std::shared_ptr<FILE> dumpFile;
		if (!fileName.empty())
			dumpFile = std::shared_ptr<FILE>(fopen(fileName.c_str(), "ab"), std::fclose);
		ResizeOptions resizeOptions;
		resizeOptions.width = width;
		resizeOptions.height = height;
		ColorOptions colorOptions;
		colorOptions.dstFourCC = format;
		FrameParameters frameArgs = { resizeOptions, colorOptions };
		for (int i = 0; i < frames; i++) {
			auto result = reader.getFrame<uint8_t>(parameters["name"], std::atoi(parameters["delay"].c_str()), frameArgs);
			if (dumpFile) {
				int status = reader.dumpFrame<uint8_t>(std::get<0>(result), frameArgs, dumpFile);
				if (status < 0)
					return;
			}

		}
	}
	catch (std::runtime_error e) {
		return;
	}

}

void checkCRC(std::map<std::string, std::string> parameters, uint64_t crc) {
	int width = std::atoi(parameters["width"].c_str());
	int height = std::atoi(parameters["height"].c_str());
	int channels = 3;
	if ((FourCC)std::atoi(parameters["format"].c_str()) == Y800)
		channels = 1;

	int frames = std::atoi(parameters["frames"].c_str());
	std::vector<uint8_t> fileRGBProcessing(width * height * channels * frames);
	{
		std::shared_ptr<FILE> readFile(fopen(parameters["dumpName"].c_str(), "rb"), fclose);
		fread(&fileRGBProcessing[0], fileRGBProcessing.size(), 1, readFile.get());
	}
	if (av_crc(av_crc_get_table(AV_CRC_32_IEEE), -1, &fileRGBProcessing[0], width * height * channels * frames) != crc) {
		ASSERT_EQ(remove(parameters["dumpName"].c_str()), 0);
		ASSERT_EQ(av_crc(av_crc_get_table(AV_CRC_32_IEEE), -1, &fileRGBProcessing[0], width * height * channels * frames), crc);
	}
	
	ASSERT_EQ(remove(parameters["dumpName"].c_str()), 0);
}

TEST(Wrapper_Init, SetTimeout) {
	const int checkTimeout = 2000;
	TensorStream reader;
	reader.enableLogs(MEDIUM);
	ASSERT_EQ(reader.initPipeline("../resources/bbb_1080x608_420_10.h264", 5, 0, 5), VREADER_OK);
	reader.setTimeout(checkTimeout);
	ASSERT_EQ(reader.getTimeout(), checkTimeout);
}

TEST(Wrapper_Init, OneThread) {
	TensorStream reader;
	reader.enableLogs(MEDIUM);
	ASSERT_EQ(reader.initPipeline("../resources/bbb_1080x608_420_10.h264", 5, 0, 5), VREADER_OK);
	std::thread pipeline(&TensorStream::startProcessing, &reader);
	std::map<std::string, std::string> parameters = { {"name", "first"}, {"delay", "0"}, {"format", std::to_string(RGB24)}, {"width", "720"}, {"height", "480"}, 
													  {"frames", "10"}, {"dumpName", "bbb_dump.yuv"} };
	//Remove artifacts from previous runs
	remove(parameters["dumpName"].c_str());
	std::thread get(getCycle, parameters, std::ref(reader));
	get.join();
	reader.endProcessing();
	pipeline.join();
	//let's compare output

	checkCRC(parameters, 249831002);
}

//several threads
TEST(Wrapper_Init, MultipleThreads) {
	TensorStream reader;
	ASSERT_EQ(reader.initPipeline("../resources/bbb_1080x608_420_10.h264", 5, 0, 5), VREADER_OK);
	std::thread pipeline(&TensorStream::startProcessing, &reader);
	std::map<std::string, std::string> parametersFirst = { {"name", "first"}, {"delay", "0"}, {"format", std::to_string(RGB24)}, {"width", "720"}, {"height", "480"},
													  {"frames", "10"}, {"dumpName", "bbb_dumpFirst.yuv"} };
	std::map<std::string, std::string> parametersSecond = { {"name", "second"}, {"delay", "-1"}, {"format", std::to_string(Y800)}, {"width", "1920"}, {"height", "1080"},
													  {"frames", "9"}, {"dumpName", "bbb_dumpSecond.yuv"} };
	//Remove artifacts from previous runs
	remove(parametersFirst["dumpName"].c_str());
	remove(parametersSecond["dumpName"].c_str());
	std::thread getFirst(getCycle, parametersFirst, std::ref(reader));
	std::thread getSecond(getCycle, parametersSecond, std::ref(reader));
	getFirst.join();
	getSecond.join();
	reader.endProcessing();
	pipeline.join();
	//let's compare output

	checkCRC(parametersFirst, 249831002);
	checkCRC(parametersSecond, 756348339);

}

void getCycleLD(std::map<std::string, std::string> parameters, TensorStream& reader) {
	try {
		int width = std::atoi(parameters["width"].c_str());
		int height = std::atoi(parameters["height"].c_str());
		FourCC format = (FourCC)std::atoi(parameters["format"].c_str());
		int frames = std::atoi(parameters["frames"].c_str());
		
		FrameParameters frameArgs = { ResizeOptions(width, height), ColorOptions(format) };
		for (int i = 0; i < frames; i++) {
			std::chrono::high_resolution_clock::time_point startTime = std::chrono::high_resolution_clock::now();
			auto result = reader.getFrame<uint8_t>(parameters["name"], std::atoi(parameters["delay"].c_str()), frameArgs);
			int sleepTime = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::high_resolution_clock::now() - startTime).count();
			//skip first several frames due to some possible additional time needed for decoded/parser to start processing
			if (i > 3) {
				ASSERT_GT(sleepTime, reader.getDelay() - 4);
				ASSERT_LT(sleepTime, reader.getDelay() + 4);
			}
		}
	}
	catch (std::runtime_error e) {
		return;
	}
}

//delay
TEST(Wrapper_Init, CheckPerformance) {
	TensorStream reader;
	reader.enableLogs(MEDIUM);
	ASSERT_EQ(reader.initPipeline("../resources/bbb_1080x608_420_10.h264", 5, 0, 5), VREADER_OK);
	std::thread pipeline(&TensorStream::startProcessing, &reader);
	std::map<std::string, std::string> parameters = { {"name", "first"}, {"delay", "0"}, {"format", std::to_string(RGB24)}, {"width", "720"}, {"height", "480"},
													  {"frames", "10"} };

	std::thread getFirst(getCycleLD, parameters, std::ref(reader));
	getFirst.join();
	reader.endProcessing();
	pipeline.join();
}

TEST(Wrapper_Init, SeveralInstances) {
	//need to check logs levels and correctness of frames
	TensorStream readerBBB;
	//readerBBB.enableLogs(-LOW);
	ASSERT_EQ(readerBBB.initPipeline("../resources/bbb_1080x608_420_10.h264", 5, 0, 5), VREADER_OK);
	TensorStream readerBilliard;
	//readerBilliard.enableLogs(-MEDIUM);
	ASSERT_EQ(readerBilliard.initPipeline("../resources/billiard_1920x1080_420_100.h264", 5, 0, 5), VREADER_OK);
	std::thread pipelineBBB(&TensorStream::startProcessing, &readerBBB);
	std::thread pipelineBilliard(&TensorStream::startProcessing, &readerBilliard);
	std::map<std::string, std::string> parametersBBB = { {"name", "BBB"}, {"delay", "0"}, {"format", std::to_string(RGB24)}, {"width", "1920"}, {"height", "1080"},
													  {"frames", "10"}, {"dumpName", "BBB_dump.yuv"} };
	std::map<std::string, std::string> parametersBilliard = { {"name", "Billiard"}, {"delay", "0"}, {"format", std::to_string(BGR24)}, {"width", "720"}, {"height", "480"},
													  {"frames", "10"}, {"dumpName", "billiard_dump.yuv"} };
	
	std::thread getBBB(getCycle, parametersBBB, std::ref(readerBBB));
	std::thread getBilliard(getCycle, parametersBilliard, std::ref(readerBilliard));
	
	getBBB.join();
	getBilliard.join();
	readerBBB.endProcessing();
	readerBilliard.endProcessing();
	pipelineBBB.join();
	pipelineBilliard.join();
	//let's compare output

	checkCRC(parametersBBB, 1775796233);
	checkCRC(parametersBilliard, 3048624823);
}

//Different CUDA devices
TEST(Wrapper_Init, DifferentGPUs) {
	TensorStream reader;
	int cudaDevicesNumber;
	auto sts = cudaGetDeviceCount(&cudaDevicesNumber);
	ASSERT_EQ(sts, VREADER_OK);
	reader.enableLogs(-LOW);
	ASSERT_EQ(reader.initPipeline("../resources/bbb_1080x608_420_10.h264", 5, cudaDevicesNumber, 5), VREADER_OK);
	ASSERT_EQ(reader.initPipeline("../resources/bbb_1080x608_420_10.h264", 5, -cudaDevicesNumber, 5), VREADER_OK);
	ASSERT_EQ(reader.initPipeline("../resources/bbb_1080x608_420_10.h264", 5, (int) (cudaDevicesNumber / 2), 5), VREADER_OK);
	
}

//Avoiding any sleeps in stream processing
TEST(Wrapper_Init, FrameRateFastLocal) {
	TensorStream reader;
	reader.skipAnalyzeStage();
	//reader.enableLogs(-HIGH);
	ASSERT_EQ(reader.initPipeline("../resources/bbb_720x480_RGB24_250.h264", 5, 0, 5, FrameRateMode::FAST), VREADER_OK);
	std::thread pipeline(&TensorStream::startProcessing, &reader);
	std::map<std::string, std::string> parameters = { {"name", "first"}, {"delay", "0"}, {"format", std::to_string(RGB24)}, {"width", "720"}, {"height", "480"},
													  {"frames", "250"}/*, {"dumpName", "output_720x480_RGB24_10.yuv"}*/ };
	std::thread getFirst(
		[](std::map<std::string, std::string> parameters, TensorStream& reader) {
			int width = std::atoi(parameters["width"].c_str());
			int height = std::atoi(parameters["height"].c_str());
			FourCC format = (FourCC)std::atoi(parameters["format"].c_str());
			int frames = std::atoi(parameters["frames"].c_str());

			std::string fileName = parameters["dumpName"];
			std::shared_ptr<FILE> dumpFile;
			if (!fileName.empty())
				dumpFile = std::shared_ptr<FILE>(fopen(fileName.c_str(), "ab"), std::fclose);
			ResizeOptions resizeOptions;
			resizeOptions.width = width;
			resizeOptions.height = height;
			ColorOptions colorOptions;
			colorOptions.dstFourCC = format;
			FrameParameters frameArgs = { resizeOptions, colorOptions };
			int maxValue = 0;
			for (int i = 0; i < frames; i++) {
				try {
					std::chrono::high_resolution_clock::time_point startTime = std::chrono::high_resolution_clock::now();
					auto result = reader.getFrame<uint8_t>(parameters["name"], std::atoi(parameters["delay"].c_str()), frameArgs);
					//we don't mind about frames indexes but only about latency
					std::chrono::high_resolution_clock::time_point endTime = std::chrono::high_resolution_clock::now();
					int latency = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
					if (i > 0)
						maxValue = std::max(maxValue, latency);
					if (dumpFile) {
						int status = reader.dumpFrame<uint8_t>(std::get<0>(result), frameArgs, dumpFile);
						if (status < 0)
							return;
					}
				}
				catch (std::runtime_error e) {
					break;
				}
			}
			//frame rate = 24, latency = 41,6
#if defined (WIN32) && defined(_DEBUG)
			//in case of Debug latency is much higher than in Release mode
			EXPECT_LT(maxValue, 25);
#else
			EXPECT_NEAR(maxValue, 3, 3);
#endif
		}, 
			parameters, 
			std::ref(reader));

	getFirst.join();
	reader.endProcessing();
	pipeline.join();
}

TEST(Wrapper_Init, FrameRateFastStream) {
	TensorStream reader;
	reader.skipAnalyzeStage();
	//reader.enableLogs(-HIGH);
	ASSERT_EQ(reader.initPipeline("rtmp://37.228.119.44:1935/vod/big_buck_bunny.mp4", 5, 0, 5, FrameRateMode::FAST), VREADER_OK);
	std::thread pipeline(&TensorStream::startProcessing, &reader);
	std::map<std::string, std::string> parameters = { {"name", "first"}, {"delay", "0"}, {"format", std::to_string(RGB24)}, {"width", "720"}, {"height", "480"},
													  {"frames", "250"}/*, {"dumpName", "output_720x480_RGB24_500.yuv"}*/ };
	std::thread getFirst(
		[](std::map<std::string, std::string> parameters, TensorStream& reader) {
		int width = std::atoi(parameters["width"].c_str());
		int height = std::atoi(parameters["height"].c_str());
		FourCC format = (FourCC)std::atoi(parameters["format"].c_str());
		int frames = std::atoi(parameters["frames"].c_str());

		std::string fileName = parameters["dumpName"];
		std::shared_ptr<FILE> dumpFile;
		if (!fileName.empty())
			dumpFile = std::shared_ptr<FILE>(fopen(fileName.c_str(), "ab"), std::fclose);
		ResizeOptions resizeOptions;
		resizeOptions.width = width;
		resizeOptions.height = height;
		ColorOptions colorOptions;
		colorOptions.dstFourCC = format;
		FrameParameters frameArgs = { resizeOptions, colorOptions };
		int minValue = INT_MAX;
		for (int i = 0; i < frames; i++) {
			try {
				std::chrono::high_resolution_clock::time_point startTime = std::chrono::high_resolution_clock::now();
				auto result = reader.getFrame<uint8_t>(parameters["name"], std::atoi(parameters["delay"].c_str()), frameArgs);
				//we don't mind about frames indexes but only about latency
				std::chrono::high_resolution_clock::time_point endTime = std::chrono::high_resolution_clock::now();
				int latency = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
				minValue = std::min(minValue, latency);
				if (dumpFile) {
					int status = reader.dumpFrame<uint8_t>(std::get<0>(result), frameArgs, dumpFile);
					if (status < 0)
						return;
				}
			}
			catch (std::runtime_error e) {
				break;
			}
		}
		//frame rate = 24, latency = 41,6
		EXPECT_LT(minValue, 42);
	},
		parameters,
		std::ref(reader));

	getFirst.join();
	reader.endProcessing();
	pipeline.join();
}

TEST(Wrapper_Init, FrameRateBlockingLocal) {
	TensorStream reader;
	//reader.enableLogs(-HIGH);
	ASSERT_EQ(reader.initPipeline("../resources/bbb_720x480_RGB24_250.h264", 5, 0, 5, FrameRateMode::BLOCKING), VREADER_OK);
	std::thread pipeline(&TensorStream::startProcessing, &reader);
	std::map<std::string, std::string> parameters = { {"name", "first"}, {"delay", "0"}, {"format", std::to_string(RGB24)}, {"width", "720"}, {"height", "480"},
													  {"frames", "250"}/*, {"dumpName", "output_720x480_RGB24_250.yuv"}*/ };
	std::thread getFirst(
		[](std::map<std::string, std::string> parameters, TensorStream& reader) {
		int width = std::atoi(parameters["width"].c_str());
		int height = std::atoi(parameters["height"].c_str());
		FourCC format = (FourCC)std::atoi(parameters["format"].c_str());
		int frames = std::atoi(parameters["frames"].c_str());

		std::string fileName = parameters["dumpName"];
		std::shared_ptr<FILE> dumpFile;
		if (!fileName.empty())
			dumpFile = std::shared_ptr<FILE>(fopen(fileName.c_str(), "ab"), std::fclose);
		ResizeOptions resizeOptions;
		resizeOptions.width = width;
		resizeOptions.height = height;
		ColorOptions colorOptions;
		colorOptions.dstFourCC = format;
		FrameParameters frameArgs = { resizeOptions, colorOptions };
		int index = 0;
		for (int i = 0; i < frames; i++) {
			try {
				auto result = reader.getFrame<uint8_t>(parameters["name"], std::atoi(parameters["delay"].c_str()), frameArgs);
				EXPECT_EQ(std::get<1>(result) - index, 1);
				index = std::get<1>(result);
				std::chrono::high_resolution_clock::time_point endTime = std::chrono::high_resolution_clock::now();
				if (dumpFile) {
					int status = reader.dumpFrame<uint8_t>(std::get<0>(result), frameArgs, dumpFile);
					if (status < 0)
						return;
				}
			}
			catch (std::runtime_error e) {
				break;
			}
		}
	},
		parameters,
		std::ref(reader));

	getFirst.join();
	reader.endProcessing();
	pipeline.join();
}

TEST(Wrapper_Init, FrameRateBlockingLocalSeveralThreads) {
	TensorStream reader;
	//reader.enableLogs(-HIGH);
	ASSERT_EQ(reader.initPipeline("../resources/bbb_720x480_RGB24_250.h264", 5, 0, 5, FrameRateMode::BLOCKING), VREADER_OK);
	std::thread pipeline(&TensorStream::startProcessing, &reader);
	std::map<std::string, std::string> parametersFirst = { {"name", "first"}, {"delay", "0"}, {"format", std::to_string(RGB24)}, {"width", "720"}, {"height", "480"},
														   {"frames", "250"}/*, {"dumpName", "first_720x480_RGB24_250.yuv"}*/ };
	std::map<std::string, std::string> parametersSecond = { {"name", "second"}, {"delay", "0"}, {"format", std::to_string(RGB24)}, {"width", "720"}, {"height", "480"},
														   {"frames", "250"}/*, {"dumpName", "second_720x480_RGB24_250.yuv"}*/ };
	
	auto getFunc = [](std::map<std::string, std::string> parameters, TensorStream& reader) {
		int width = std::atoi(parameters["width"].c_str());
		int height = std::atoi(parameters["height"].c_str());
		FourCC format = (FourCC)std::atoi(parameters["format"].c_str());
		int frames = std::atoi(parameters["frames"].c_str());

		std::string fileName = parameters["dumpName"];
		std::shared_ptr<FILE> dumpFile;
		if (!fileName.empty())
			dumpFile = std::shared_ptr<FILE>(fopen(fileName.c_str(), "ab"), std::fclose);
		ResizeOptions resizeOptions;
		resizeOptions.width = width;
		resizeOptions.height = height;
		ColorOptions colorOptions;
		colorOptions.dstFourCC = format;
		FrameParameters frameArgs = { resizeOptions, colorOptions };
		int index = 0;
		for (int i = 0; i < frames; i++) {
			try {
				auto result = reader.getFrame<uint8_t>(parameters["name"], std::atoi(parameters["delay"].c_str()), frameArgs);
				EXPECT_EQ(std::get<1>(result) - index, 1);
				index = std::get<1>(result);
				std::chrono::high_resolution_clock::time_point endTime = std::chrono::high_resolution_clock::now();
				if (dumpFile) {
					int status = reader.dumpFrame<uint8_t>(std::get<0>(result), frameArgs, dumpFile);
					if (status < 0)
						return;
				}
			}
			catch (std::runtime_error e) {
				break;
			}
		}
	};
	
	std::thread getFirst(
		getFunc,
		parametersFirst,
		std::ref(reader));
	std::thread getSecond(
		getFunc,
		parametersSecond,
		std::ref(reader));

	getFirst.join();
	getSecond.join();
	reader.endProcessing();
	pipeline.join();
}

//this test just check correctness of reading stream in blocking mode, but it doesn't have any sense to use blocking in case of RTMP stream
TEST(Wrapper_Init, FrameRateBlockingStream) {
	TensorStream reader;
	//reader.enableLogs(-HIGH);
	ASSERT_EQ(reader.initPipeline("rtmp://37.228.119.44:1935/vod/big_buck_bunny.mp4", 5, 0, 5, FrameRateMode::BLOCKING), VREADER_OK);
	std::thread pipeline(&TensorStream::startProcessing, &reader);
	std::map<std::string, std::string> parameters = { {"name", "first"}, {"delay", "0"}, {"format", std::to_string(RGB24)}, {"width", "720"}, {"height", "480"},
													  {"frames", "250"}/*, {"dumpName", "output_720x480_RGB24_250.yuv"}*/ };
	std::thread getFirst(
		[](std::map<std::string, std::string> parameters, TensorStream& reader) {
		int width = std::atoi(parameters["width"].c_str());
		int height = std::atoi(parameters["height"].c_str());
		FourCC format = (FourCC)std::atoi(parameters["format"].c_str());
		int frames = std::atoi(parameters["frames"].c_str());

		std::string fileName = parameters["dumpName"];
		std::shared_ptr<FILE> dumpFile;
		if (!fileName.empty())
			dumpFile = std::shared_ptr<FILE>(fopen(fileName.c_str(), "ab"), std::fclose);
		ResizeOptions resizeOptions;
		resizeOptions.width = width;
		resizeOptions.height = height;
		ColorOptions colorOptions;
		colorOptions.dstFourCC = format;
		FrameParameters frameArgs = { resizeOptions, colorOptions };
		int index = 0;
		for (int i = 0; i < frames; i++) {
			try {
				auto result = reader.getFrame<uint8_t>(parameters["name"], std::atoi(parameters["delay"].c_str()), frameArgs);
				EXPECT_EQ(std::get<1>(result) - index, 1);
				index = std::get<1>(result);
				std::chrono::high_resolution_clock::time_point endTime = std::chrono::high_resolution_clock::now();
				if (dumpFile) {
					int status = reader.dumpFrame<uint8_t>(std::get<0>(result), frameArgs, dumpFile);
					if (status < 0)
						return;
				}
			}
			catch (std::runtime_error e) {
				break;
			}
		}
	},
		parameters,
		std::ref(reader));

	getFirst.join();
	reader.endProcessing();
	pipeline.join();
}

bool getCycleBatch(std::map<std::string, std::string> parameters, std::vector<int> batch, TensorStream& reader) {
	int width = std::atoi(parameters["width"].c_str());
	int height = std::atoi(parameters["height"].c_str());
	FourCC format = (FourCC)std::atoi(parameters["format"].c_str());
	int frames = std::atoi(parameters["frames"].c_str());

	std::string fileName = parameters["dumpName"];
	std::shared_ptr<FILE> dumpFile;
	if (!fileName.empty())
		dumpFile = std::shared_ptr<FILE>(fopen(fileName.c_str(), "ab"), std::fclose);
	ResizeOptions resizeOptions;
	resizeOptions.width = width;
	resizeOptions.height = height;
	ColorOptions colorOptions;
	colorOptions.dstFourCC = format;
	FrameParameters frameArgs = { resizeOptions, colorOptions };
	auto result = reader.getFrameAbsolute<uint8_t>(batch, frameArgs);
	if (dumpFile) {
		for (auto& frame : result) {
			int status = reader.dumpFrame<uint8_t>(frame, frameArgs, dumpFile);
			if (status < 0)
				return VREADER_ERROR;
		}
	}
	return VREADER_OK;
}

TEST(Wrapper_Batch, ZeroBatch) {
	TensorStream reader;
	ASSERT_EQ(reader.initPipeline("../resources/tennis_2s.mp4", 0, 0, 0), VREADER_OK);
	std::vector<int> frames = {};
	std::map<std::string, std::string> parameters = { {"frames", std::to_string(frames.size())}, {"format", std::to_string(RGB24)}, {"width", "720"}, {"height", "480"},
														   {"dumpName", "bbb_dumpFirst.yuv"} };
	//Remove artifacts from previous runs
	remove(parameters["dumpName"].c_str());
	bool sts = getCycleBatch(parameters, frames, std::ref(reader));
	ASSERT_EQ(sts, VREADER_OK);
	reader.endProcessing();
}

TEST(Wrapper_Batch, FrameOutOfBounds) {
	TensorStream reader;
	ASSERT_EQ(reader.initPipeline("../resources/tennis_2s.mp4", 0, 0, 0), VREADER_OK);
	std::vector<int> frames = { 0, 100, 250, 120 };
	std::map<std::string, std::string> parameters = { {"frames", std::to_string(frames.size())}, {"format", std::to_string(RGB24)}, {"width", "720"}, {"height", "480"},
													  {"dumpName", "bbb_dumpFirst.yuv"} };
	
	//Remove artifacts from previous runs
	remove(parameters["dumpName"].c_str());
	ASSERT_THROW(getCycleBatch(parameters, frames, std::ref(reader)), std::runtime_error);
	reader.endProcessing();
}

//several threads
TEST(Wrapper_Batch, MultipleThreadsEqual) {
	TensorStream reader;
	ASSERT_EQ(reader.initPipeline("../resources/tennis_2s.mp4", 0, 0, 0), VREADER_OK);
	std::vector<int> frames = { 0, 25, 55, 70 };
	std::map<std::string, std::string> parametersFirst = { {"frames", std::to_string(frames.size())}, {"format", std::to_string(RGB24)}, {"width", "720"}, {"height", "480"},
													       {"dumpName", "bbb_dumpFirst.yuv"} };
	std::map<std::string, std::string> parametersSecond = { {"frames", std::to_string(frames.size())}, {"format", std::to_string(NV12)}, {"width", "1920"}, {"height", "1080"},
													        {"dumpName", "bbb_dumpSecond.yuv"} };
	//Remove artifacts from previous runs
	remove(parametersFirst["dumpName"].c_str());
	remove(parametersSecond["dumpName"].c_str());
	std::thread getFirst(getCycleBatch, parametersFirst, frames, std::ref(reader));
	std::thread getSecond(getCycleBatch, parametersSecond, frames, std::ref(reader));
	getFirst.join();
	getSecond.join();
	reader.endProcessing();
	//let's compare output

	checkCRC(parametersFirst, 1512214004);
	checkCRC(parametersSecond, 749536786);
}

//several threads
TEST(Wrapper_Batch, MultipleThreadsDifferent) {
	TensorStream reader;
	ASSERT_EQ(reader.initPipeline("../resources/tennis_2s.mp4", 0, 0, 0), VREADER_OK);
	std::vector<int> framesFirst = { 0, 25, 55 };
	std::vector<int> framesSecond = { 60, 0, 200, 220, 70 };
	std::map<std::string, std::string> parametersFirst = { {"frames", std::to_string(framesFirst.size())}, {"format", std::to_string(RGB24)}, {"width", "720"}, {"height", "480"},
														   {"dumpName", "bbb_dumpFirst.yuv"} };
	std::map<std::string, std::string> parametersSecond = { {"frames", std::to_string(framesSecond.size())}, {"format", std::to_string(NV12)}, {"width", "1920"}, {"height", "1080"},
															{"dumpName", "bbb_dumpSecond.yuv"} };
	//Remove artifacts from previous runs
	remove(parametersFirst["dumpName"].c_str());
	remove(parametersSecond["dumpName"].c_str());
	std::thread getFirst(getCycleBatch, parametersFirst, framesFirst, std::ref(reader));
	std::thread getSecond(getCycleBatch, parametersSecond, framesSecond, std::ref(reader));
	getFirst.join();
	getSecond.join();
	reader.endProcessing();
	//let's compare output

	checkCRC(parametersFirst, 2769188104);
	checkCRC(parametersSecond, 391182750);

}

//several instances
TEST(Wrapper_Batch, MultipleInstancesEqual) {
	TensorStream readerFirst;
	ASSERT_EQ(readerFirst.initPipeline("../resources/tennis_2s.mp4", 0, 0, 0), VREADER_OK);
	TensorStream readerSecond;
	ASSERT_EQ(readerSecond.initPipeline("../resources/tennis_2s.mp4", 0, 0, 0), VREADER_OK);
	std::vector<int> frames = { 0, 25, 55, 70 };
	std::map<std::string, std::string> parametersFirst = { {"frames", std::to_string(frames.size())}, {"format", std::to_string(RGB24)}, {"width", "720"}, {"height", "480"},
														   {"dumpName", "bbb_dumpFirst.yuv"} };
	std::map<std::string, std::string> parametersSecond = { {"frames", std::to_string(frames.size())}, {"format", std::to_string(NV12)}, {"width", "1920"}, {"height", "1080"},
															{"dumpName", "bbb_dumpSecond.yuv"} };
	//Remove artifacts from previous runs
	remove(parametersFirst["dumpName"].c_str());
	remove(parametersSecond["dumpName"].c_str());
	std::thread getFirst(getCycleBatch, parametersFirst, frames, std::ref(readerFirst));
	std::thread getSecond(getCycleBatch, parametersSecond, frames, std::ref(readerSecond));
	getFirst.join();
	getSecond.join();
	readerFirst.endProcessing();
	readerSecond.endProcessing();
	//let's compare output

	checkCRC(parametersFirst, 1512214004);
	checkCRC(parametersSecond, 749536786);
}

TEST(Wrapper_Batch, InstanceGPUMemory) {
	TensorStream reader;
	ASSERT_EQ(reader.initPipeline("../resources/tennis_2s.mp4", 0, 0, 0), VREADER_OK);
	size_t memFreeBefore, memFreeAfter, memTotal;
	cudaMemGetInfo(&memFreeBefore, &memTotal);
	//create 10 instances and measure GPU/RAM
	for (int i = 0; i < 10; i++) {
		TensorStream reader;
		ASSERT_EQ(reader.initPipeline("../resources/tennis_2s.mp4", 0, 0, 0), VREADER_OK);
	}
	cudaMemGetInfo(&memFreeAfter, &memTotal);
	//used memory in mb
	ASSERT_LT((memFreeBefore - memFreeAfter) / 1024 / 1024, 10);
}

#ifdef WIN32
TEST(Wrapper_Batch, InstanceCPUMemory) {
	TensorStream reader;
	ASSERT_EQ(reader.initPipeline("../resources/tennis_2s.mp4", 0, 0, 0), VREADER_OK);
	size_t memBefore, memAfter;
	auto myHandle = GetCurrentProcess();
	PROCESS_MEMORY_COUNTERS pmc;
	if (GetProcessMemoryInfo(myHandle, &pmc, sizeof(pmc)))
		memBefore = pmc.WorkingSetSize;
	//create 10 instances and measure GPU/RAM
	for (int i = 0; i < 10; i++) {
		TensorStream reader;
		ASSERT_EQ(reader.initPipeline("../resources/tennis_2s.mp4", 0, 0, 0), VREADER_OK);
	}
	if (GetProcessMemoryInfo(myHandle, &pmc, sizeof(pmc)))
		memAfter = pmc.WorkingSetSize;

	//used memory in mb
	ASSERT_LT((memAfter - memBefore) / 1024 / 1024, 10);
}
#endif

TEST(Wrapper_Batch, MultipleInstancesDifferent) {
	TensorStream readerFirst;
	ASSERT_EQ(readerFirst.initPipeline("../resources/tennis_2s.mp4", 0, 0, 0), VREADER_OK);
	TensorStream readerSecond;
	ASSERT_EQ(readerSecond.initPipeline("../resources/tennis_2s.mp4", 0, 0, 0), VREADER_OK);
	std::vector<int> framesFirst = { 0, 25, 55 };
	std::vector<int> framesSecond = { 60, 0, 200, 220, 70 };
	std::map<std::string, std::string> parametersFirst = { {"frames", std::to_string(framesFirst.size())}, {"format", std::to_string(RGB24)}, {"width", "720"}, {"height", "480"},
														   {"dumpName", "bbb_dumpFirst.yuv"} };
	std::map<std::string, std::string> parametersSecond = { {"frames", std::to_string(framesSecond.size())}, {"format", std::to_string(NV12)}, {"width", "1920"}, {"height", "1080"},
															{"dumpName", "bbb_dumpSecond.yuv"} };
	//Remove artifacts from previous runs
	remove(parametersFirst["dumpName"].c_str());
	remove(parametersSecond["dumpName"].c_str());
	std::thread getFirst(getCycleBatch, parametersFirst, framesFirst, std::ref(readerFirst));
	std::thread getSecond(getCycleBatch, parametersSecond, framesSecond, std::ref(readerSecond));
	getFirst.join();
	getSecond.join();
	readerFirst.endProcessing();
	readerSecond.endProcessing();
	//let's compare output

	checkCRC(parametersFirst, 2769188104);
	checkCRC(parametersSecond, 391182750);
}

//this test should be at the end
TEST(Wrapper_Init, OneThreadHang) {
	bool ended = false;
	std::thread mainThread([&ended]() {
		TensorStream reader;
		reader.enableLogs(MEDIUM);
		ASSERT_EQ(reader.initPipeline("../resources/bbb_1080x608_420_10.h264", 5, 0, 5), VREADER_OK);
		std::thread pipeline(&TensorStream::startProcessing, &reader);
		std::map<std::string, std::string> parameters = { {"name", "first"}, {"delay", "0"}, {"format", std::to_string(RGB24)}, {"width", "720"}, {"height", "480"},
														  {"frames", "10"}, {"dumpName", "bbb_dump.yuv"} };
		//Remove artifacts from previous runs
		remove(parameters["dumpName"].c_str());
		std::thread get(getCycle, parameters, std::ref(reader));
		//wait for some processing happened
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		//Close Reader before joining any thread, expect no hangs at the end of program
		reader.endProcessing();
		get.join();
		reader.endProcessing();
		pipeline.join();
		//let's compare output
		ended = true;
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(5000));
	ASSERT_EQ(ended, true);
	mainThread.join();
}