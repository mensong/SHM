// testMulti.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <iostream>
#include "../shm/SHM.h"
#include <intrin.h>

SHM shm;
const char* pIn = "123456789987654321123456789987654321123456789987654321123456789987654321123456789987654321123456789987654321";
size_t pInLen = 0;

int main(int argc, char** argv)
{
	pInLen = strlen(pIn);
	bool b = false;
	int testCount = 10000;
	if (!shm.Init(L"testMulti", 20002, 64))
	{
		std::cout << "Init error." << std::endl;
		return -1;
	}

	if (argc == 1)
	{
		std::cout << "正在测试写数据..." << std::endl;		
		for (int i = 0; i < testCount; ++i)
		{
			b = shm.Write(pIn, pInLen, i);
			if (!b)
			{
				std::cout << "写数据出错:" << i << std::endl;
			}
			if (i == testCount - 1)
				i = 0;
		}
	}
	else if (argc == 2)
	{
		for (size_t i = 0; i < testCount; i++)
		{
			int n = shm.Read(NULL, 0, i);
			if (n <= 0)
			{
				std::cout << "读取出错" << std::endl;

				if (i == testCount - 1)
					i = 0;
				continue;
			}

			char* pOut = new char[n + 1];
			n = shm.Read(pOut, n, i);
			pOut[n] = 0;
			std::cout << pOut << std::endl;
			delete[] pOut;

			if (i == testCount - 1)
				i = 0;
		}
	}
	else if (argc == 3)
	{
		for (size_t i = 0; i < testCount; i++)
		{
			b = shm.Remove(i);
			if (!b)
			{
				std::cout << "删除数据出错:" << i << std::endl;
			}
			if (i == testCount - 1)
				i = 0;
		}
	}

    return 0;
}
