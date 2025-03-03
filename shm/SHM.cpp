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
    , m_indexInfoBufSize(0)
    , m_pNoUsedIdxWarehouseBuf(NULL)
	, m_noUsedIdxWarehouseBufSize(0)
    , m_pBlockBuf(NULL)
    , m_blockCount(0)
    , m_blockSize(0)
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

    //索引所需的空间
    m_indexInfoBufSize = 1 + m_blockCount;//例子：blockCount|(startBlockIdx0)0|(startBlockIdx1)3|(startBlockIdx2)5
 
	//索引仓库所需空间的块数（多少个int64）。
    // 使用多个int64来存储，每个int64存储64个bit，每个bit代表一个block的索引号是否被使用，使用了设置为0，未使用设置为1。
	// 然后使用 num & (-num) 来获取最低位的1，来获取未使用的索引号。
    m_noUsedIdxWarehouseBufSize = m_blockCount / 63;//一个int64存储63个bit，最后一个bit不用。
    if (m_blockCount % 63 != 0)
        m_noUsedIdxWarehouseBufSize++;

    LARGE_INTEGER allocSize;
    allocSize.QuadPart =
        m_indexInfoBufSize * sizeof(int) + //block索引号空间大小
        m_noUsedIdxWarehouseBufSize * sizeof(__int64) + //block未使用的索引号空间大小
        //thisBlockDataSize+ data + nextBlockIdx 
        blockCount * sizeof(int) + blockCount * sizeof(int) + (blockCount * (blockSize * sizeof(char))); 

    bool created = false;
    //打开共享的文件对象。
    m_hMapFile = OpenFileMapping(
        FILE_MAP_ALL_ACCESS, 
        FALSE, 
        shmName);
    if (!m_hMapFile || m_hMapFile == INVALID_HANDLE_VALUE)
    {
        //创建共享文件。
        m_hMapFile = CreateFileMapping(
            INVALID_HANDLE_VALUE, 
            NULL, 
            PAGE_READWRITE/*物理页*/, 
            allocSize.HighPart/*高位*/,
            allocSize.LowPart/*低位*/,
            shmName);
        //DWORD err = GetLastError();
        created = true;
    }

    if (!m_hMapFile || m_hMapFile == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    //拷贝索引与block数据到共享文件里。
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
        m_pIndexInfoBuf[0] = blockCount;//写blockCount
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

    //先把dataID旧所用到的blockIdx设为可用（如果有）
    traverseBlockIdx(dataID, [&](int blockIdx)->bool {
        setBlockIndexNoUsed(blockIdx);
        return true;
    });

    int headIdx = -1;
    char* pLastMMWrite = NULL;
    const char* pDataToWrite = pData;
    int writeSize = dataSize;
    int endIdx = -1;

    //写大于一个block的数据
    while (writeSize > m_blockSize)
    {
        int newIdx = getNoUsedBlockIdx();
        if (newIdx < 0)
            return false;
        if (headIdx == -1)
            headIdx = newIdx;

        //节点间的连接序号
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
    //写小于一个block的数据
    if (writeSize > 0)
    {
        int newIdx = getNoUsedBlockIdx();
        if (newIdx < 0)
            return false;
        if (headIdx == -1)
            headIdx = newIdx;

        //节点间的连接序号
        if (pLastMMWrite)
            memcpy(pLastMMWrite, (const void*)&newIdx, sizeof(int));

        char* p = m_pBlockBuf + (newIdx * (sizeof(int) + m_blockSize + sizeof(int)));//dataSize+data+nextIdx
        memcpy(p, (const void*)&writeSize, sizeof(int));
        p += sizeof(int);
        memcpy(p, pDataToWrite, writeSize);
        p += writeSize;
        pLastMMWrite = p;
        memcpy(pLastMMWrite, (const void*)&endIdx, sizeof(int));//结束时写-1表示已经是结束block

        setBlockIndexUsed(newIdx);
    }

	//写index info
	m_pIndexInfoBuf[1 + dataID] = headIdx;//1：扣除最前面的blockCount

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
        int dataSize = *((int*)pBlock);    //当前block中存储的数据长度
        char* pData = pBlock + sizeof(int);//数据开始位置
        blockIdx = *((int*)(pData + dataSize));//下一个数据位置

        if (pOutBuf && outBufSize > 0)
        {
            int readSize = min(dataSize, outBufSize);
            memcpy(pOutBuf + dataSizeTotal, pData, readSize);

            if (outBufSize <= dataSize)
            {//已读完
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
    
    //移除indexinfo
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

        //找到仓库
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

int SHM::getNoUsedBlockIdx()
{
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
	//    return 63;//最后一个bit用于正负标志，不使用
	default:
		break;
	}
    return -1;
}

bool SHM::setBlockIndexUsed(int blockIdx)
{
    int warehouse = -1;
    int idxInAWarehouse = -1;
    if (!whereInWarehouse(blockIdx, &warehouse, &idxInAWarehouse))
        return false;

    m_pNoUsedIdxWarehouseBuf[warehouse] = 
        setBit0(m_pNoUsedIdxWarehouseBuf[warehouse], idxInAWarehouse);

    return true;
}

bool SHM::setBlockIndexNoUsed(int blockIdx)
{
    int warehouse = -1;
    int idxInAWarehouse = -1;
    if (!whereInWarehouse(blockIdx, &warehouse, &idxInAWarehouse))
        return false;

    m_pNoUsedIdxWarehouseBuf[warehouse] =
        setBit1(m_pNoUsedIdxWarehouseBuf[warehouse], idxInAWarehouse);

    return true;
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
        int dataSize = *((int*)pBlock);    //当前block中存储的数据长度
        char* pData = pBlock + sizeof(int);//数据开始位置
        blockIdx = *((int*)(pData + dataSize));//下一个数据位置
    }

    return true;
}

bool SHM::whereInWarehouse(int blockIdx, int* warehouseIdx, int* idxInAWarehouse)
{
    if (blockIdx < 0 || blockIdx >= m_blockCount)
        return false;

	//找到仓库的索引
    *warehouseIdx = blockIdx / 63;
    if (*warehouseIdx >= m_noUsedIdxWarehouseBufSize)
        return false;

	//找到在仓库中的索引
    *idxInAWarehouse = blockIdx % 63;

    return true;
}
