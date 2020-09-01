#include <gtest/gtest.h>
#include "fstream"

int main(int argc, char *argv[])
{
	testing::InitGoogleTest(&argc, argv);
	//::testing::GTEST_FLAG(filter) = "Parser_Analyze_Broken.LastFrameRepeat";

	//Disable cout output from library
	std::cout.setstate(std::ios_base::failbit);
	return RUN_ALL_TESTS();
}