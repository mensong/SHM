#include "shm.h"

__int64 setBit0(__int64 num, int pos)
{
    return num & ~(1ULL << pos);
}
__int64 setBit1(__int64 num, int pos)
{
    return num | (1ULL << pos);
}
bool isBit1(__int64 num, int pos)
{
    return (num & (1ULL << pos)) != 0;
}

SHM::SHM()
    : m_hMapFile(NULL)
    , m_pBuf(NULL)
	, m_pIndexInfoBuf(NULL)
	, m_pBlockBuf(NULL)
    , m_indexInfoBufSize(0)
    , m_pNoUsedIdxWarehouseBuf(NULL)
	, m_noUsedIdxWarehouseBufSize(0)
    , m_blockCount(100)
    , m_blockSize(512)
    , m_bitmap_blocks(NULL)
    , m_meta_bitmap (0)  // ÿһλ��ʾ��Ӧ���Ƿ��п��з�����0=�ޣ�1=�У�
{
}

SHM::~SHM()
{
    //FlushViewOfFile(lpMapAddr, strTest.length() + 1);

    m_mutex.Lock();

    if (m_pBuf)
    {
        UnmapViewOfFile(m_pBuf);
        m_pBuf = NULL;
        m_pIndexInfoBuf = NULL;
        m_pBlockBuf = NULL;
    }
    if (m_hMapFile && m_hMapFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(m_hMapFile);
        m_hMapFile = NULL;
    }
    
    uninit_partition_manager();

    m_mutex.Unlock();
}

bool SHM::Init(const TCHAR* shmName, int blockCount, int blockSize)
{
    TCHAR intialLockName[MAX_PATH] = TEXT("");
    lstrcat(intialLockName, TEXT("Init_"));
    lstrcat(intialLockName, shmName);
    GlobalMutex initLocker;
    initLocker.Init();
    initLocker.Lock();

    m_blockCount = blockCount;
    m_blockSize = blockSize;

    //��������Ŀռ�
    m_indexInfoBufSize = 1 + m_blockCount;//���ӣ�blockCount|(startBlockIdx0)0|(startBlockIdx1)3|(startBlockIdx2)5
 
	//�����ֿ�����ռ�Ŀ��������ٸ�int64����
    // ʹ�ö��int64���洢��ÿ��int64�洢64��bit��ÿ��bit����һ��block���������Ƿ�ʹ�ã�ʹ��������Ϊ0��δʹ������Ϊ1��
	// Ȼ��ʹ�� num & (-num) ����ȡ���λ��1������ȡδʹ�õ������š�
    m_noUsedIdxWarehouseBufSize = m_blockCount / 63;//һ��int64�洢63��bit�����һ��bit���á�
    if (m_blockCount % 63 != 0)
        m_noUsedIdxWarehouseBufSize++;

    LARGE_INTEGER allocSize;
    allocSize.QuadPart =
        m_indexInfoBufSize * sizeof(int) + //block�����ſռ��С
        m_noUsedIdxWarehouseBufSize * sizeof(__int64) + //blockδʹ�õ������ſռ��С
        //thisBlockDataSize+ data + nextBlockIdx 
        blockCount * sizeof(int) + blockCount * sizeof(int) + (blockCount * (blockSize * sizeof(char))); 

    bool created = false;
    //�򿪹�����ļ�����
    m_hMapFile = OpenFileMapping(
        FILE_MAP_ALL_ACCESS, 
        FALSE, 
        shmName);
    if (!m_hMapFile || m_hMapFile == INVALID_HANDLE_VALUE)
    {
        //���������ļ���
        m_hMapFile = CreateFileMapping(
            INVALID_HANDLE_VALUE, 
            NULL, 
            PAGE_READWRITE/*����ҳ*/, 
            allocSize.HighPart/*��λ*/,
            allocSize.LowPart/*��λ*/,
            shmName);
        //DWORD err = GetLastError();
        created = true;
    }

    if (!m_hMapFile || m_hMapFile == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    //����������block���ݵ������ļ��
    m_pBuf = (char*)MapViewOfFile(
        m_hMapFile,
        FILE_MAP_ALL_ACCESS,
        0, 
        0, 
        allocSize.QuadPart);
    if (!m_pBuf)
    {
        CloseHandle(m_hMapFile);
        m_hMapFile = NULL;
        return false;
    }
    if (created)
    {
        memset(m_pBuf, 0, allocSize.QuadPart);
    }

	m_pIndexInfoBuf = (int*)m_pBuf;
    if (created)
    {
        m_pIndexInfoBuf[0] = blockCount;//дblockCount
        for (int i = 1; i < m_indexInfoBufSize; i++)
            m_pIndexInfoBuf[i] = -1;
    }

	m_pNoUsedIdxWarehouseBuf = (__int64*)(m_pIndexInfoBuf + m_indexInfoBufSize);
    if (created)
    {
        for (int i = 0; i < m_noUsedIdxWarehouseBufSize; i++)
            m_pNoUsedIdxWarehouseBuf[i] = 0b0111111111111111111111111111111111111111111111111111111111111111;
    }

    m_pBlockBuf = (char*)(m_pNoUsedIdxWarehouseBuf + m_noUsedIdxWarehouseBufSize);

    TCHAR dataLockName[MAX_PATH] = TEXT("");
    lstrcat(dataLockName, TEXT("Data_"));
    lstrcat(dataLockName, shmName);
    m_mutex.Init(dataLockName);

    init_partition_manager();

    return true;
}

bool SHM::Write(const char* pData, int dataSize, int dataID)
{
    if (dataID >= m_blockCount)
        return false;

    m_mutex.Lock();

    if (!m_pIndexInfoBuf || !m_pBlockBuf)
    {
        m_mutex.Unlock();
        return false;
    }

    //�Ȱ�dataID�����õ���blockIdx��Ϊ���ã�����У�
    traverseBlockIdx(dataID, [&](int blockIdx)->bool {
        setBlockIndexNoUsed(blockIdx);
        return true;
    });

    int headIdx = -1;
    char* pLastMMWrite = NULL;
    const char* pDataToWrite = pData;
    int writeSize = dataSize;
    int endIdx = -1;

    //д����һ��block������
    while (writeSize > m_blockSize)
    {
        int newIdx = getNoUsedBlockIdx();
        if (newIdx < 0)
            return false;
        if (headIdx == -1)
            headIdx = newIdx;

        //�ڵ����������
        if (pLastMMWrite)
            memcpy(pLastMMWrite, (const void*)&newIdx, sizeof(int));

		char* p = m_pBlockBuf + (newIdx * (sizeof(int) + m_blockSize + sizeof(int)));//dataSize+data+nextIdx
        memcpy(p, (const void*)&m_blockSize, sizeof(int));
        p += sizeof(int);
        memcpy(p, pDataToWrite, m_blockSize);
        p += m_blockSize;
        pLastMMWrite = p;

        setBlockIndexUsed(newIdx);

        writeSize -= m_blockSize;
        pDataToWrite += m_blockSize;		
    }
    //дС��һ��block������
    if (writeSize > 0)
    {
        int newIdx = getNoUsedBlockIdx();
        if (newIdx < 0)
            return false;
        if (headIdx == -1)
            headIdx = newIdx;

        //�ڵ����������
        if (pLastMMWrite)
            memcpy(pLastMMWrite, (const void*)&newIdx, sizeof(int));

        char* p = m_pBlockBuf + (newIdx * (sizeof(int) + m_blockSize + sizeof(int)));//dataSize+data+nextIdx
        memcpy(p, (const void*)&writeSize, sizeof(int));
        p += sizeof(int);
        memcpy(p, pDataToWrite, writeSize);
        p += writeSize;
        pLastMMWrite = p;
        memcpy(pLastMMWrite, (const void*)&endIdx, sizeof(int));//����ʱд-1��ʾ�Ѿ��ǽ���block

        setBlockIndexUsed(newIdx);
    }

	//дindex info
	m_pIndexInfoBuf[1 + dataID] = headIdx;//1���۳���ǰ���blockCount

    m_mutex.Unlock();
    return headIdx != -1;
}

int SHM::Read(char* pOutBuf, int outBufSize, int dataID)
{
    if (dataID >= m_blockCount)
        return -1;

    m_mutex.Lock();

    if (!m_pIndexInfoBuf || !m_pBlockBuf)
    {
        m_mutex.Unlock();
        return -1;
    }

    int headBlockIdx = m_pIndexInfoBuf[1 + dataID];
    if (headBlockIdx < 0 || headBlockIdx >= m_blockCount)
        return -1;

	int dataSizeTotal = 0;
    int blockIdx = headBlockIdx;
    while (blockIdx > -1)
    {
        char* pBlock = m_pBlockBuf + (blockIdx * (sizeof(int) + m_blockSize + sizeof(int)));
        int dataSize = *((int*)pBlock);    //��ǰblock�д洢�����ݳ���
        char* pData = pBlock + sizeof(int);//���ݿ�ʼλ��
        blockIdx = *((int*)(pData + dataSize));//��һ������λ��

        if (pOutBuf && outBufSize > 0)
        {
            int readSize = min(dataSize, outBufSize);
            memcpy(pOutBuf + dataSizeTotal, pData, readSize);

            if (outBufSize <= dataSize)
            {//�Ѷ���
                //pOutBuf += readSize;
                dataSizeTotal += readSize;
                break;
            }

            outBufSize -= readSize;
        }

        dataSizeTotal += dataSize;
    }
            
    m_mutex.Unlock();
	return dataSizeTotal;
}

bool SHM::Remove(int dataID)
{
	if (dataID >= m_blockCount)
		return false;

	m_mutex.Lock();

    if (!m_pIndexInfoBuf)
    {
        m_mutex.Unlock();
        return false;
    }
    
    //�Ƴ�indexinfo
    int blockIdx = m_pIndexInfoBuf[1 + dataID];
	if (blockIdx != -1)
		setBlockIndexNoUsed(blockIdx);
    m_pIndexInfoBuf[1 + dataID] = -1;

	m_mutex.Unlock();
    return true;
}

void SHM::ListDataIDs(std::vector<int>& dataIDs)
{
    m_mutex.Lock();

    if (!m_pIndexInfoBuf)
    {
        m_mutex.Unlock();
        return;
    }

    for (int i = 0; i < m_blockCount; i++)
    {
        if (m_pIndexInfoBuf[1 + i] >= 0)
            dataIDs.push_back(m_pIndexInfoBuf[1 + i]);
    }

    m_mutex.Unlock();
}

#if 0
int SHM::IsBlockUsed(int blockIdx)
{
    if (blockIdx >= m_blockCount || !m_pNoUsedIdxWarehouseBuf)
        return -1;

    m_mutex.Lock();

    int ret = -1;
    do
    {
        if (blockIdx >= m_blockCount)
        {
            ret = -1;
			break;
        }

        //�ҵ��ֿ�
        int warehouse = -1;
        int idxInAWarehouse = -1;
        if (!whereInWarehouse(blockIdx, &warehouse, &idxInAWarehouse))
        {
            ret = -1;
            break;
        }

        if (isBit1(m_pNoUsedIdxWarehouseBuf[warehouse], idxInAWarehouse))
        {
            ret = 0;
            break;
        }
        else
        {
            ret = 1;
            break;
        }
    } while (false);
    	
	m_mutex.Unlock();
	return ret;
}
#endif

int SHM::getNoUsedBlockIdx()
{
#if 0
	int idxRet = -1;
	do
	{
        for (int i = 0; i < m_noUsedIdxWarehouseBufSize; i++)
        {
            int n = getLowestNoZeroBitIndex(m_pNoUsedIdxWarehouseBuf[i]);
			if (n != -1)
			{
				idxRet = n + (i * 63);
                break;
			}

        }
	} while (false);

    if (idxRet >= m_blockCount)
		idxRet = -1;
	return idxRet;
#else
    return find_unused_partition();
#endif
}

int SHM::getLowestNoZeroBitIndex(__int64 warehouse)
{
    __int64 lowest = warehouse & (-warehouse);

	switch (lowest)
	{
	case 0b0000000000000000000000000000000000000000000000000000000000000001:
		return 0;
    case 0b0000000000000000000000000000000000000000000000000000000000000010:
        return 1;
    case 0b0000000000000000000000000000000000000000000000000000000000000100:
        return 2;
    case 0b0000000000000000000000000000000000000000000000000000000000001000:
        return 3;
    case 0b0000000000000000000000000000000000000000000000000000000000010000:
        return 4;
    case 0b0000000000000000000000000000000000000000000000000000000000100000:
        return 5;
    case 0b0000000000000000000000000000000000000000000000000000000001000000:
        return 6;
    case 0b0000000000000000000000000000000000000000000000000000000010000000:
        return 7;
    case 0b0000000000000000000000000000000000000000000000000000000100000000:
        return 8;
    case 0b0000000000000000000000000000000000000000000000000000001000000000:
        return 9;
    case 0b0000000000000000000000000000000000000000000000000000010000000000:
        return 10;
    case 0b0000000000000000000000000000000000000000000000000000100000000000:
        return 11;
    case 0b0000000000000000000000000000000000000000000000000001000000000000:
        return 12;
    case 0b0000000000000000000000000000000000000000000000000010000000000000:
        return 13;
    case 0b0000000000000000000000000000000000000000000000000100000000000000:
        return 14;
    case 0b0000000000000000000000000000000000000000000000001000000000000000:
        return 15;
    case 0b0000000000000000000000000000000000000000000000010000000000000000:
        return 16;
    case 0b0000000000000000000000000000000000000000000000100000000000000000:
        return 17;
    case 0b0000000000000000000000000000000000000000000001000000000000000000:
        return 18;
    case 0b0000000000000000000000000000000000000000000010000000000000000000:
        return 19;
    case 0b0000000000000000000000000000000000000000000100000000000000000000:
        return 20;
    case 0b0000000000000000000000000000000000000000001000000000000000000000:
        return 21;
    case 0b0000000000000000000000000000000000000000010000000000000000000000:
        return 22;
    case 0b0000000000000000000000000000000000000000100000000000000000000000:
        return 23;
    case 0b0000000000000000000000000000000000000001000000000000000000000000:
        return 24;
    case 0b0000000000000000000000000000000000000010000000000000000000000000:
        return 25;
    case 0b0000000000000000000000000000000000000100000000000000000000000000:
        return 26;
    case 0b0000000000000000000000000000000000001000000000000000000000000000:
        return 27;
    case 0b0000000000000000000000000000000000010000000000000000000000000000:
        return 28;
    case 0b0000000000000000000000000000000000100000000000000000000000000000:
        return 29;
    case 0b0000000000000000000000000000000001000000000000000000000000000000:
        return 30;
    case 0b0000000000000000000000000000000010000000000000000000000000000000:
        return 31;
    case 0b0000000000000000000000000000000100000000000000000000000000000000:
        return 32;
    case 0b0000000000000000000000000000001000000000000000000000000000000000:
        return 33;
    case 0b0000000000000000000000000000010000000000000000000000000000000000:
        return 34;
    case 0b0000000000000000000000000000100000000000000000000000000000000000:
        return 35;
    case 0b0000000000000000000000000001000000000000000000000000000000000000:
        return 36;
    case 0b0000000000000000000000000010000000000000000000000000000000000000:
        return 37;
    case 0b0000000000000000000000000100000000000000000000000000000000000000:
        return 38;
    case 0b0000000000000000000000001000000000000000000000000000000000000000:
        return 39;
    case 0b0000000000000000000000010000000000000000000000000000000000000000:
        return 40;
    case 0b0000000000000000000000100000000000000000000000000000000000000000:
        return 41;
    case 0b0000000000000000000001000000000000000000000000000000000000000000:
        return 42;
    case 0b0000000000000000000010000000000000000000000000000000000000000000:
        return 43;
    case 0b0000000000000000000100000000000000000000000000000000000000000000:
        return 44;
    case 0b0000000000000000001000000000000000000000000000000000000000000000:
        return 45;
    case 0b0000000000000000010000000000000000000000000000000000000000000000:
        return 46;
    case 0b0000000000000000100000000000000000000000000000000000000000000000:
        return 47;
    case 0b0000000000000001000000000000000000000000000000000000000000000000:
        return 48;
    case 0b0000000000000010000000000000000000000000000000000000000000000000:
        return 49;
    case 0b0000000000000100000000000000000000000000000000000000000000000000:
        return 50;
    case 0b0000000000001000000000000000000000000000000000000000000000000000:
        return 51;
    case 0b0000000000010000000000000000000000000000000000000000000000000000:
        return 52;
    case 0b0000000000100000000000000000000000000000000000000000000000000000:
        return 53;
    case 0b0000000001000000000000000000000000000000000000000000000000000000:
        return 54;
    case 0b0000000010000000000000000000000000000000000000000000000000000000:
        return 55;
    case 0b0000000100000000000000000000000000000000000000000000000000000000:
        return 56;
    case 0b0000001000000000000000000000000000000000000000000000000000000000:
        return 57;
    case 0b0000010000000000000000000000000000000000000000000000000000000000:
        return 58;
    case 0b0000100000000000000000000000000000000000000000000000000000000000:
        return 59;
    case 0b0001000000000000000000000000000000000000000000000000000000000000:
        return 60;
    case 0b0010000000000000000000000000000000000000000000000000000000000000:
        return 61;
    case 0b0100000000000000000000000000000000000000000000000000000000000000:
        return 62;
    //case 0b1000000000000000000000000000000000000000000000000000000000000000:
	//    return 63;//���һ��bit����������־����ʹ��
	default:
		break;
	}
    return -1;
}

bool SHM::setBlockIndexUsed(int blockIdx)
{
#if 0
    int warehouse = -1;
    int idxInAWarehouse = -1;
    if (!whereInWarehouse(blockIdx, &warehouse, &idxInAWarehouse))
        return false;

    m_pNoUsedIdxWarehouseBuf[warehouse] = 
        setBit0(m_pNoUsedIdxWarehouseBuf[warehouse], idxInAWarehouse);

    return true;
#else
    mark_partition_used(blockIdx);
    return true;
#endif
}

bool SHM::setBlockIndexNoUsed(int blockIdx)
{
#if 0
    int warehouse = -1;
    int idxInAWarehouse = -1;
    if (!whereInWarehouse(blockIdx, &warehouse, &idxInAWarehouse))
        return false;

    m_pNoUsedIdxWarehouseBuf[warehouse] =
        setBit1(m_pNoUsedIdxWarehouseBuf[warehouse], idxInAWarehouse);

    return true;
#else
    mark_partition_unused(blockIdx);
    return true;
#endif
}

bool SHM::traverseBlockIdx(int dataID, FN_TraverseBlockIdxCallback cb)
{
    if (dataID >= m_blockCount)
        return false;

    if (!m_pIndexInfoBuf || !m_pBlockBuf)
    {
        return false;
    }

    int headBlockIdx = m_pIndexInfoBuf[1 + dataID];
    if (headBlockIdx < 0 || headBlockIdx >= m_blockCount)
        return false;

    int blockIdx = headBlockIdx;
    while (blockIdx > -1)
    {
        if (!cb(blockIdx))
            return true;

        char* pBlock = m_pBlockBuf + (blockIdx * (sizeof(int) + m_blockSize + sizeof(int)));
        int dataSize = *((int*)pBlock);    //��ǰblock�д洢�����ݳ���
        char* pData = pBlock + sizeof(int);//���ݿ�ʼλ��
        blockIdx = *((int*)(pData + dataSize));//��һ������λ��
    }

    return true;
}

#if 0
bool SHM::whereInWarehouse(int blockIdx, int* warehouseIdx, int* idxInAWarehouse)
{
    if (blockIdx < 0 || blockIdx >= m_blockCount)
        return false;

	//�ҵ��ֿ������
    *warehouseIdx = blockIdx / 63;
    if (*warehouseIdx >= m_noUsedIdxWarehouseBufSize)
        return false;

	//�ҵ��ڲֿ��е�����
    *idxInAWarehouse = blockIdx % 63;

    return true;
}
#endif

void SHM::init_partition_manager()
{
    int group64Count = (m_blockCount + BITMAP_BLOCK_SIZE - 1) / BITMAP_BLOCK_SIZE;
    m_bitmap_blocks = (uint64_t*)calloc(group64Count, sizeof(uint64_t));
    m_meta_bitmap = (1ULL << group64Count) - 1;  // ��ʼ���п���п���
}

void SHM::mark_partition_used(int global_idx)
{
    int bitmap_block_idx = global_idx / BITMAP_BLOCK_SIZE;
    int bit_pos = global_idx % BITMAP_BLOCK_SIZE;
    m_bitmap_blocks[bitmap_block_idx] |= (1ULL << bit_pos);

    // �������������Ԫλͼ�еı��
    if (m_bitmap_blocks[bitmap_block_idx] == UINT64_MAX) 
    {
        m_meta_bitmap &= ~(1ULL << bitmap_block_idx);
    }
}

void SHM::mark_partition_unused(int global_idx)
{
    int bitmap_block_idx = global_idx / BITMAP_BLOCK_SIZE;
    int bit_pos = global_idx % BITMAP_BLOCK_SIZE;
    m_bitmap_blocks[bitmap_block_idx] &= ~(1ULL << bit_pos);

    // ����Ԫλͼ���
    m_meta_bitmap |= (1ULL << bitmap_block_idx);
}

int SHM::find_unused_partition()
{
    // ����1: ��Ԫλͼ�п��ٶ�λ�п��еĿ�
    uint64_t meta_mask = m_meta_bitmap;
    while (meta_mask != 0) 
    {
        unsigned long idx = 0;
        _BitScanForward64(&idx, meta_mask);  // �ҵ���һ����0λ
        int block_idx = idx;
        meta_mask ^= (1ULL << block_idx);                // ����Ѽ��Ŀ���

        // ����2: ��Ŀ����ڲ��Ҿ������λ
        uint64_t block = m_bitmap_blocks[block_idx];
        uint64_t inverted_block = ~block;
        if (inverted_block != 0) 
        {
            unsigned long idx = 0;
            _BitScanForward64(&idx, inverted_block);
            int bit_pos = idx;
            int global_idx = block_idx * BITMAP_BLOCK_SIZE + bit_pos;
            if (global_idx >= m_blockCount)
                global_idx = -1;
            return global_idx;  // ����ȫ�ַ�������
        }
    }
    return -1;  // �޿��÷���
}

void SHM::uninit_partition_manager()
{
    //if (m_bitmap_blocks)
    //{
    //    free(m_bitmap_blocks);
    //    m_bitmap_blocks = NULL;
    //}
}
