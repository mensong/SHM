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
    SHM();
    ~SHM();

    bool Init(const TCHAR* shmName, int blockCount, int blockSize);

    bool Write(const char* pData, int dataSize, int dataID);

    //�ɴ���pOutBuf=NULLʱ������ݳ��ȡ�
    int Read(char* pOutBuf, int outBufSize, int dataID);

    bool Remove(int dataID);

    void ListDataIDs(std::vector<int>& dataIDs);

#if 0
	//-1:����0:δʹ�ã�1:��ʹ��
	int IsBlockUsed(int blockIdx);
#endif

protected:
    //���δʹ�õĿ���ţ�����-1��ʾ�Ѿ�û�п����ʹ��
    int getNoUsedBlockIdx();
    //���һ��__int64������͵ķ�0λ��ţ�����0b0001����0��0b0010����1
    int getLowestNoZeroBitIndex(__int64 warehouse);
    //���ÿ����Ϊ��ʹ��
    bool setBlockIndexUsed(int blockIdx);
    //���ÿ����Ϊδʹ��
    bool setBlockIndexNoUsed(int blockIdx);

    //����dataID���õ���blockIdx
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
#define BITMAP_BLOCK_SIZE 64          // ÿ�������64������
    uint64_t* m_bitmap_blocks;   // ��̬����λͼ����
    uint64_t m_meta_bitmap;

    void init_partition_manager();
    void mark_partition_used(int global_idx);
    void mark_partition_unused(int global_idx);
    int find_unused_partition();
    void uninit_partition_manager();
#pragma endregion

};

