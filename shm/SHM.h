#pragma once
#include <windows.h>
#include <vector>
#include <functional>
#include "GlobalMutex.h"

/*
blockCount(3) startBlockIdx startBlockIdx startBlockIdx|unused indexs bits|blockSize blockData nextBlockIdx|blockSize blockData nextBlockIdx|blockSize blockData nextBlockIdx
|-----------------------索引区域-----------------------|  未使用的索引位  |-------------------------------------------数据区域-----------------------------------------------|
*/
class SHM
{
public:
    SHM();
    ~SHM();

    bool Init(const TCHAR* shmName, int blockCount, int blockSize);

    bool Write(const char* pData, int dataSize, int dataID);

    //可传入pOutBuf=NULL时获得数据长度。
    int Read(char* pOutBuf, int outBufSize, int dataID);

    bool Remove(int dataID);

    void ListDataIDs(std::vector<int>& dataIDs);

#if 0
	//-1:出错；0:未使用；1:已使用
	int IsBlockUsed(int blockIdx);
#endif

protected:
    //获得未使用的块序号，返回-1表示已经没有块可以使用
    int getNoUsedBlockIdx();
    //获得一个__int64数的最低的非0位序号，例如0b0001返回0，0b0010返回1
    int getLowestNoZeroBitIndex(__int64 warehouse);
    //设置块序号为已使用
    bool setBlockIndexUsed(int blockIdx);
    //设置块序号为未使用
    bool setBlockIndexNoUsed(int blockIdx);

    //遍历dataID的用到的blockIdx
    typedef std::function<bool(int blockIdx)> FN_TraverseBlockIdxCallback;
    bool traverseBlockIdx(int dataID, FN_TraverseBlockIdxCallback cb);

protected:
    int m_blockCount;
    int m_blockSize;
    
    HANDLE m_hMapFile;

    char* m_pBuf;

    int* m_pIndexInfoBuf;
    int m_indexInfoBufSize;

	__int64* m_pNoUsedIdxWarehouseBuf;
    int m_noUsedIdxWarehouseBufSize;
#if 0
    bool whereInWarehouse(int blockIdx, int* warehouseIdx, int* idxInAWarehouse);
#endif

	char* m_pBlockBuf;

    GlobalMutex m_mutex;

#pragma region bitmap_allocation
#define BITMAP_BLOCK_SIZE 64          // 每个块管理64个分区
    uint64_t* m_bitmap_blocks;   // 动态分配位图数组
    uint64_t m_meta_bitmap;

    void init_partition_manager();
    void mark_partition_used(int global_idx);
    void mark_partition_unused(int global_idx);
    int find_unused_partition();
    void uninit_partition_manager();
#pragma endregion

};

