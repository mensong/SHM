#pragma once
#include <windows.h>
#include <vector>
#include <functional>
#include "GlobalMutex.h"
/*
size(3) dataID idx1 idx2 size(1) idx3 size(1) idx4 INT_MIN|unused indexs bits|blockSize blockData|blockSize blockData|blockSize blockData|blockSize blockData
|------------------------索引区域-------------------------|  未使用的索引位  |----------------------------------数据区域-------------------------------------|
*/
class SHM
{
public:
    //遍历索引信息的回调，返回false则中断遍历。
    typedef std::function<bool(int infoSize, int dataID, int* blockIdxList, int blockIdxListSize)> FN_IndexInfoCallback;

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

    //遍历索引信息
    bool TraverseIndexInfo(FN_IndexInfoCallback cb);

    //获得dataID所用到的BlockIdx列表
    bool ListBlockIndexs(int dataID, std::vector<int>& blockIdxList);

protected:
    int getNoUsedBlockIdx();
    int getNoZeroBitNum(__int64 warehouse);
    bool setBlockIndexUsed(int blockIdx);
    bool setBlockIndexNoUsed(int blockIdx);
    //遍历索引信息
    bool traverseIndexInfo(int* indexInfoBuf, int indexInfoBufSize, FN_IndexInfoCallback cb);

protected:
    int m_blockCount;
    int m_blockSize;
    
    HANDLE m_hMapFile;

    char* m_pBuf;

	int* m_pIndexInfoBuf;
    int m_indexInfoBufSize;
    int* m_cacheIndexInfoBufForWrite;

	__int64* m_pNoUsedIdxWarehouseBuf;
    int m_noUsedIdxWarehouseBufSize;
    bool whereInWarehouse(int blockIdx, int* warehouseIdx, int* idxInAWarehouse);

	char* m_pBlockBuf;

    GlobalMutex m_mutex;
};

