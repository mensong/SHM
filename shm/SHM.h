#pragma once
#include <windows.h>
#include <vector>
#include <map>
#include "GlobalMutex.h"

/*
-1 dataID idx1 idx2 -1 idx3 -1 idx4 -1 -1|unused indexs bits|blockSize blockData|blockSize blockData|blockSize blockData|blockSize blockData
|---------------索引区域-----------------|  未使用的索引位  |----------------------------------数据区域-------------------------------------|
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
    void ListDataIDs(std::vector<int>& idxs);

protected:
	void getIndeies(int dataID, std::vector<int>& idxs);
	void getAllIndeies(std::map<int, std::vector<int>>& idxs);
    int getNoUsedIdx();
    int getNoZeroBitNum(__int64 warehouse);
    bool setBlockIndexUsed(int idx);
    bool setBlockIndexNoUsed(int idx);

protected:
    int m_blockCount;
    int m_blockSize;
    
    HANDLE m_hMapFile;

    char* m_pBuf;

	int* m_pIndexBuf;
    int m_indexBufSize;

	__int64* m_pNoUsedIdxWarehouseBuf;
    int m_noUsedIdxWarehouseBufSize;

	char* m_pBlockBuf;

    GlobalMutex m_mutex;

    int m_lastUsedIdx;


    DWORD times = 0;
};

