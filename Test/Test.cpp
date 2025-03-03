// LogHost.cpp
//

#include <iostream>
#include <vector>
#include <string>
#include <process.h>
#include "../shm/SHM.h"
#include "simdb.hpp"
#include <intrin.h>

SHM shm;
const char* pIn = "123456789987654321123456789987654321123456789987654321123456789987654321123456789987654321123456789987654321";
size_t pInLen = 0;

std::atomic<bool> g_bExit = false;

#ifdef _WIN32
unsigned __stdcall
#else
void*
#endif 
_process_thread_write(void* arg)
{
	bool b = false;
	while (!g_bExit)
	{
		//if (::GetTickCount() - st > 20000)
		//	break;

		b = shm.Write(pIn, pInLen, 0);
		if (!b)
		{
			std::cout << "写数据出错" << std::endl;
		}
	}

	return 0;
}

#ifdef _WIN32
unsigned __stdcall
#else
void*
#endif 
_process_thread_read(void* arg)
{
	char* pOut = new char[pInLen + 1];
	while (!g_bExit)
	{
		//if (::GetTickCount() - st > 20000)
		//	break;

		int n = shm.Read(NULL, 0, 0);
		if (n == 0)
		{
			std::cout << "读取出错" << std::endl;
			continue;
		}
		n = shm.Read(pOut, pInLen, 0);
		pOut[n] = 0;
	}
	delete[] pOut;

	return 0;
}

void smallTest()
{
	{
		char pRead[10] = { 0 };
		int readN;
		SHM shm;
		shm.Init(L"mensong1", 30, 1);

		shm.Write("0", 1, 0);
		memset(pRead, 0, 10);
		readN = shm.Read(pRead, 10, 0);

		//int u = shm.IsBlockUsed(0);
		//u = shm.IsBlockUsed(1);

		shm.Write("12", 2, 1);
		memset(pRead, 0, 10);
		readN = shm.Read(pRead, 10, 1);

		shm.Write("3", 1, 2);
		memset(pRead, 0, 10);
		readN = shm.Read(pRead, 10, 2);

		shm.Write("4", 1, 3);
		memset(pRead, 0, 10);
		readN = shm.Read(pRead, 10, 3);

		shm.Write("567", 3, 4);
		memset(pRead, 0, 10);
		readN = shm.Read(pRead, 10, 4);


		shm.Write("0", 1, 0);
		shm.Write("567", 3, 4);
		shm.Write("3", 1, 2);
		shm.Write("4", 1, 3);
		shm.Write("12", 2, 1);

		shm.Remove(0);
		shm.Remove(1);
		shm.Remove(2);
		shm.Remove(3);
		shm.Remove(4);

		memset(pRead, 0, 10);
		readN = shm.Read(pRead, 10, 4);

		memset(pRead, 0, 10);
		readN = shm.Read(pRead, 10, 2);

		memset(pRead, 0, 10);
		readN = shm.Read(pRead, 10, 3);

		memset(pRead, 0, 10);
		readN = shm.Read(pRead, 10, 1);

		memset(pRead, 0, 10);
		readN = shm.Read(pRead, 10, 0);
	}
}

void testSimDB()
{
	simdb db("test", 64, 1005000);
	int testCount = 500000;

	auto testWriteFunc = [&]()->void
	{
		bool b = false;
		char* pRead = new char[pInLen + 1];
		pRead[pInLen] = 0;
		DWORD st = ::GetTickCount();
		for (int i = 0; i < testCount; ++i)
		{
			b = db.put(std::to_string(i).c_str(), pIn, pInLen);			
			if (!b)
			{
				std::cout << "写数据出错:" << i << std::endl;
			}
			pRead[0] = 0;
			b = db.get(std::to_string(i).c_str(), pRead, pInLen);
			pRead[pInLen] = 0;
			if (!b)
			{
				std::cout << "读数据出错:" << i << std::endl;
			}
			else if (strcmp(pIn, pRead) != 0)
			{
				std::cout << "读写数据出错:" << i << std::endl;
			}
		}
		DWORD t = ::GetTickCount() - st;
		std::cout << "simdb Write " << testCount << "次耗时:" << t << "毫秒" << std::endl;
		std::cout << "simdb Write 速度:" << (double)testCount / (t / 1000.0) << " 次/秒" << std::endl;
		delete[] pRead;
	};
	testWriteFunc();
	testWriteFunc();
	testWriteFunc();

	getchar();
}

int main(int argc, char** argv)
{
	pInLen = strlen(pIn);

	//testSimDB();
	//smallTest();
	//bitmapTest();
	//return 0;


	if (!shm.Init(L"mensong", 20002, 64))
	{
		std::cout << "Init error." << std::endl;
		return -1;
	}

	std::cout << "正在测试单线程读写中..." << std::endl;

	int testCount = 10000;

	{
		DWORD st = ::GetTickCount();
		for (int i = 0; i < testCount; i++)
		{
			int dataID = shm.AppendWrite(pIn, pInLen);
			if (dataID < 0)
			{
				std::cout << "增加数据出错:" << i << std::endl;
			}
		}
		DWORD t = ::GetTickCount() - st;
		std::cout << "AppendWrite " << testCount << "次耗时:" << t << "毫秒" << std::endl;
		std::string sSpeed = (t == 0 ?
			("大于" + std::to_string(testCount)) :
			std::to_string((double)testCount / (t / 1000.0)));
		std::cout << "Write 速度:" << sSpeed << " 次/秒" << std::endl;
	}

	auto testWriteFunc = [&]()->void
	{
		bool b = false;
		char* pRead = new char[pInLen + 1];
		pRead[pInLen] = 0;
		DWORD st = ::GetTickCount();
		for (int i = 0; i < testCount; ++i)
		{
			b = shm.Write(pIn, pInLen, i);
			if (!b)
			{
				std::cout << "写数据出错:" << i << std::endl;
			}

			//pRead[0] = 0;
			//b = shm.Read(pRead, pInLen, i);
			//if (!b)
			//{
			//	std::cout << "读数据出错:" << i << std::endl;
			//}
			//else if (strcmp(pIn, pRead) != 0)
			//{
			//	std::cout << "读写数据出错:" << i << std::endl;
			//}
		}
		DWORD t = ::GetTickCount() - st;
		std::cout << "Write " << testCount << "次耗时:" << t << "毫秒" << std::endl;
		std::string sSpeed = (t == 0 ?
			("大于" + std::to_string(testCount)) :
			std::to_string((double)testCount / (t / 1000.0)));
		std::cout << "Write 速度:" << sSpeed << " 次/秒" << std::endl;
		delete[] pRead;
	};
	testWriteFunc();
	testWriteFunc();
	testWriteFunc();

	{
		std::vector<int> dataIDs;
		shm.ListDataIDs(dataIDs);
		std::cout << "ListDataIDs count " << dataIDs.size() << " times." << std::endl;
	}

	{
		DWORD st = ::GetTickCount();
		for (size_t i = 0; i < testCount; i++)
		{
			int n = shm.Read(NULL, 0, i);
			if (n == 0)
			{
				std::cout << "读取出错" << std::endl;
				continue;
			}
			char* pOut = new char[n + 1];
			n = shm.Read(pOut, n, i);
			pOut[n] = 0;
			//std::cout << pOut << std::endl;
			if (strcmp(pIn, pOut) != 0)
			{
				std::cout << "测试不通过" << std::endl;
			}

			delete[] pOut;
		}
		DWORD t = ::GetTickCount() - st;
		std::cout << "Read " << testCount << "次耗时:" << t << "毫秒" << std::endl;
		std::string sSpeed = (t == 0 ?
			("大于" + std::to_string(testCount)) :
			std::to_string((double)testCount / (t / 1000.0)));
		std::cout << "Read 速度:" << sSpeed << " 次/秒" << std::endl;
	}

	{
		//多线程测试
		for (int i = 0; i < 16; ++i)
		{
			unsigned  uiThreadID = 0;
			unsigned  uiThreadID1 = 0;
			HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, _process_thread_write, (void*)NULL, 0, &uiThreadID);
			HANDLE hThread1 = (HANDLE)_beginthreadex(NULL, 0, _process_thread_read, (void*)NULL, 0, &uiThreadID1);
		}

		std::cout << "正在测试多线程读写中，按回车键退出线程" << std::endl;
	}

	getchar();
	g_bExit = true;
	Sleep(2000);

	{
		std::cout << "正在测试删除数据中..." << std::endl;
		DWORD st = ::GetTickCount();
		for (size_t i = 0; i < testCount; i++)
		{
			shm.Remove(i);
		}
		DWORD t = ::GetTickCount() - st;
		std::cout << "Remove " << testCount << "次耗时:" << t << "毫秒" << std::endl;
		std::string sSpeed = (t == 0 ?
			("大于" + std::to_string(testCount)) :
			std::to_string((double)testCount / (t / 1000.0)));
		std::cout << "Remove 速度:" << sSpeed << " 次/秒" << std::endl;
	}

	{
		std::vector<int> dataIDs;
		shm.ListDataIDs(dataIDs);
		std::cout << "ListDataIDs count " << dataIDs.size() << " times." << std::endl;
	}

	std::cout << "按回车键退出程序" << std::endl;
	getchar();

	return 0;
}
