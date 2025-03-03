// LogHost.cpp
//

#include <iostream>
#include <vector>
#include <string>
#include <process.h>
#include "../shm/SHM.h"
#include <bitset>
#include <atomic>
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


#define TOTAL_PARTITIONS 100000000    // 1亿个分区
#define BITMAP_BLOCK_SIZE 64                 // 每个块管理64个分区
uint64_t* m_bitmap_blocks;              // 动态分配位图数组
uint64_t m_meta_bitmap = 0;  // 每一位表示对应块是否有空闲分区（0=无，1=有）

void init_partition_manager() {
	int group64Count = (TOTAL_PARTITIONS + BITMAP_BLOCK_SIZE - 1) / BITMAP_BLOCK_SIZE;
	m_bitmap_blocks = (uint64_t*)calloc(group64Count, sizeof(uint64_t));
	m_meta_bitmap = (1ULL << group64Count) - 1;  // 初始所有块均有空闲
}


int find_unused_partition() {
	// 步骤1: 在元位图中快速定位有空闲的块
	uint64_t meta_mask = m_meta_bitmap;
	while (meta_mask != 0) {		
		unsigned long idx = 0;
		_BitScanForward64(&idx, meta_mask);  // 找到第一个非0位
		int block_idx = idx;
		meta_mask ^= (1ULL << block_idx);                // 清除已检查的块标记

		// 步骤2: 在目标块内查找具体空闲位
		uint64_t block = m_bitmap_blocks[block_idx];
		uint64_t inverted_block = ~block;
		if (inverted_block != 0) {
			unsigned long idx = 0;
			_BitScanForward64(&idx, inverted_block);
			int bit_pos = idx;
			int global_idx = block_idx * BITMAP_BLOCK_SIZE + bit_pos;
			return global_idx;  // 返回全局分区索引
		}
	}
	return -1;  // 无可用分区
}

void mark_partition_used(int global_idx) {
	int block_idx = global_idx / BITMAP_BLOCK_SIZE;
	int bit_pos = global_idx % BITMAP_BLOCK_SIZE;
	m_bitmap_blocks[block_idx] |= (1ULL << bit_pos);

	// 若块已满，清除元位图中的标记
	if (m_bitmap_blocks[block_idx] == UINT64_MAX) {
		m_meta_bitmap &= ~(1ULL << block_idx);
	}
}

void mark_partition_unused(int global_idx) {
	int block_idx = global_idx / BITMAP_BLOCK_SIZE;
	int bit_pos = global_idx % BITMAP_BLOCK_SIZE;
	m_bitmap_blocks[block_idx] &= ~(1ULL << bit_pos);

	// 更新元位图标记
	m_meta_bitmap |= (1ULL << block_idx);
}


void bitmapTest()
{
	init_partition_manager();
	int n = find_unused_partition();

	mark_partition_used(0);
	n = find_unused_partition();
	mark_partition_used(1);
	n = find_unused_partition();
	mark_partition_used(2);
	n = find_unused_partition();
	mark_partition_used(3);
	n = find_unused_partition();
	mark_partition_used(4);
	n = find_unused_partition();
	mark_partition_used(5);
	n = find_unused_partition();
	mark_partition_used(6);
	n = find_unused_partition();
	mark_partition_used(7);
	n = find_unused_partition();
	mark_partition_used(8);
	n = find_unused_partition();
	mark_partition_used(9);
	n = find_unused_partition();
	mark_partition_used(10);
	n = find_unused_partition();
	mark_partition_used(11);
	n = find_unused_partition();
	mark_partition_used(12);
	n = find_unused_partition();
	mark_partition_used(13);
	n = find_unused_partition();
	mark_partition_unused(0);
	n = find_unused_partition();
}

int main(int argc, char** argv)
{
	pInLen = strlen(pIn);

	//testSimDB();
	//smallTest();
	bitmapTest();
	return 0;


	if (!shm.Init(L"mensong", 2000002, 64))
	{
		std::cout << "Init error." << std::endl;
		return -1;
	}

	std::cout << "正在测试单线程读写中..." << std::endl;

	int testCount = 1000000;

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
