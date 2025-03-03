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
    typedef std::function<bool(int blockIdx)> FN_TraverseBlockIdxCallback;

public:
    SHM();
    ~SHM();

    bool Init(const TCHAR* shmName, int blockCount, int blockSize);

    bool Write(const char* pData, int dataSize, int dataID);

    //可传入pOutBuf=NULL时获得数据长度。
    int Read(char* pOutBuf, int outBufSize, int dataID);

    bool Remove(int dataID);

    void ListDataIDs(std::vector<int>& dataIDs);

	//-1:出错；0:未使用；1:已使用
	int IsBlockUsed(int blockIdx);

    //遍历dataID的用到的blockIdx
    bool TraverseBlockIdx(int dataID, FN_TraverseBlockIdxCallback cb);

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
    bool whereInWarehouse(int blockIdx, int* warehouseIdx, int* idxInAWarehouse);

	char* m_pBlockBuf;

    GlobalMutex m_mutex;
};

