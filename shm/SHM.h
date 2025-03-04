#pragma once
#include <windows.h>
#include <vector>
#include <functional>
#include "GlobalMutex.h"

/*
blockCount(3) startBlockIdx startBlockIdx startBlockIdx|unused indexs bits|blockSize blockData nextBlockIdx|blockSize blockData nextBlockIdx|blockSize blockData nextBlockIdx
|-----------------------��������-----------------------|  δʹ�õ�����λ  |-------------------------------------------��������-----------------------------------------------|
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

    //���һ�����ݣ�������dataID
    int AppendWrite(const char* pData, int dataSize);

    //�ɴ���pOutBuf=NULLʱ������ݳ��ȡ�
    int Read(char* pOutBuf, int outBufSize, int dataID);

    bool Remove(int dataID);

    void ListDataIDs(std::vector<int>& dataIDs);

	//-1:����0:δʹ�ã�1:��ʹ��
	int IsBlockUsed(int blockIdx);

    //����dataID���õ���blockIdx
    bool TraverseBlockIdx(int dataID, FN_TraverseBlockIdxCallback cb);

protected:
    //���δʹ�õĿ���ţ�����-1��ʾ�Ѿ�û�п����ʹ��
    int getNoUsedBlockIdx();
    //���һ��__int64������͵ķ�0λ��ţ�����0b0001����0��0b0010����1
    int getLowestNoZeroBitIndex(__int64 warehouse);
    //���ÿ����Ϊ��ʹ��
    bool setBlockIndexUsed(int blockIdx);
    //���ÿ����Ϊδʹ��
    bool setBlockIndexNoUsed(int blockIdx);

    bool write(const char* pData, int dataSize, int dataID);
    int read(char* pOutBuf, int outBufSize, int dataID);
    bool remove(int dataID);
    //����dataID���õ���blockIdx
    bool traverseBlockIdx(int dataID, FN_TraverseBlockIdxCallback cb);

protected:
    int m_blockCount;
    int m_blockSize;
    
    HANDLE m_hMapFile;

    char* m_pBuf;

    int* m_pMetaDataBuf;

    int* m_pIndexInfoBuf;
    int m_indexInfoBufSize;

    unsigned __int64* m_pNoUsedIdxWarehouseBuf;
    int m_noUsedIdxWarehouseBufSize;
    bool whereInWarehouse(int blockIdx, int* warehouseIdx, int* idxInAWarehouse);

	char* m_pBlockBuf;

    GlobalMutex m_mutex;
};

