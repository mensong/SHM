#include "shm.h"

void setBit0(unsigned __int64& num, int pos)
{
    num &= ~(1ULL << pos);
}
void setBit1(unsigned __int64& num, int pos)
{
    num |= (1ULL << pos);
}
bool isBit1(unsigned __int64 num, int pos)
{
    return (num & (1ULL << pos)) != 0;
}

SHM::SHM()
    : m_hMapFile(NULL)
    , m_pBuf(NULL)
    , m_pMetaDataBuf(NULL)
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

    //元数据所需空间：BlockCount + BlockSize
    int metaDataSize = 2;//blockCount|blockSize

    //索引所需的空间
    m_indexInfoBufSize = m_blockCount;//例子：(startBlockIdx0)0|(startBlockIdx1)3|(startBlockIdx2)5
 
	//索引仓库所需空间的块数（多少个int64）。
    // 使用多个int64来存储，每个int64存储64个bit，每个bit代表一个block的索引号是否被使用，使用了设置为0，未使用设置为1。
    m_noUsedIdxWarehouseBufSize = m_blockCount / 64;//一个int64存储64个bit
    if (m_blockCount % 64 != 0)
        m_noUsedIdxWarehouseBufSize++;

    LARGE_INTEGER allocSize;
    allocSize.QuadPart =
		metaDataSize * sizeof(int) +
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

    //元数据区
    m_pMetaDataBuf = (int*)m_pBuf;
    if (created)
    {
        m_pMetaDataBuf[0] = blockCount;
        m_pMetaDataBuf[1] = blockSize;
    }

    //索引区
	m_pIndexInfoBuf = (int*)(m_pMetaDataBuf + metaDataSize);
    if (created)
    {
        for (int i = 0; i < m_indexInfoBufSize; i++)
            m_pIndexInfoBuf[i] = -1;
    }

    //未使用块记录区
	m_pNoUsedIdxWarehouseBuf = (unsigned __int64*)(m_pIndexInfoBuf + m_indexInfoBufSize);
    if (created)
    {
        for (int i = 0; i < m_noUsedIdxWarehouseBufSize; i++)
            m_pNoUsedIdxWarehouseBuf[i] = 0b1111111111111111111111111111111111111111111111111111111111111111;
    }

    //数据区
    m_pBlockBuf = (char*)(m_pNoUsedIdxWarehouseBuf + m_noUsedIdxWarehouseBufSize);

    TCHAR dataLockName[MAX_PATH] = TEXT("");
    lstrcat(dataLockName, TEXT("Data_"));
    lstrcat(dataLockName, shmName);
    m_mutex.Init(dataLockName);

    return true;
}

bool SHM::Write(const char* pData, int dataSize, int dataID)
{
    m_mutex.Lock();
    bool b = write(pData, dataSize, dataID);
    m_mutex.Unlock();
    return b;
}

int SHM::AppendWrite(const char* pData, int dataSize)
{
    m_mutex.Lock();

    int dataIDCreated = -1;
    do
    {
        if (!m_pIndexInfoBuf)
            break;

        for (int i = 0; i < m_blockCount; i++)
        {
            if (m_pIndexInfoBuf[i] < 0)
            {
                dataIDCreated = i;
                break;
            }
        }
        if (dataIDCreated < 0)
            break;

        if (!write(pData, dataSize, dataIDCreated))
        {
            dataIDCreated = -1;
            break;
        }
    } while (false);

    m_mutex.Unlock();
    return dataIDCreated;
}

int SHM::Read(char* pOutBuf, int outBufSize, int dataID)
{
    m_mutex.Lock();

    int readsize = read(pOutBuf, outBufSize, dataID);
            
    m_mutex.Unlock();
	return readsize;
}

bool SHM::Remove(int dataID)
{
	m_mutex.Lock();

    //Sleep(20000);

    bool b = remove(dataID);

	m_mutex.Unlock();
    return b;
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
        if (m_pIndexInfoBuf[i] >= 0)
            dataIDs.push_back(m_pIndexInfoBuf[i]);
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

bool SHM::TraverseBlockIdx(int dataID, FN_TraverseBlockIdxCallback cb)
{
    m_mutex.Lock();
    bool b = traverseBlockIdx(dataID, cb);
    m_mutex.Unlock();
    return b;
}

bool SHM::write(const char* pData, int dataSize, int dataID)
{
    if (dataID >= m_blockCount)
        return false;

    if (!m_pIndexInfoBuf || !m_pBlockBuf)
    {
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
    m_pIndexInfoBuf[dataID] = headIdx;
    //FlushViewOfFile(&m_pIndexInfoBuf[dataID], sizeof(int));

    return headIdx != -1;
}

int SHM::read(char* pOutBuf, int outBufSize, int dataID)
{
    if (dataID >= m_blockCount)
        return -1;

    if (!m_pIndexInfoBuf || !m_pBlockBuf)
    {
        return -1;
    }

    int headBlockIdx = m_pIndexInfoBuf[dataID];
    if (headBlockIdx < 0 || headBlockIdx >= m_blockCount)
        return -1;

    int dataSizeTotal = 0;
    int blockIdx = headBlockIdx;
    for (int i = 0; i < m_blockCount && blockIdx > -1; i++)
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

    return dataSizeTotal;
}

bool SHM::remove(int dataID)
{
    if (dataID >= m_blockCount)
        return false;

    if (!m_pIndexInfoBuf)
    {
        return false;
    }

    //设置所用到的block为未使用
    traverseBlockIdx(dataID, [&](int blockIdx)->bool {
        setBlockIndexNoUsed(blockIdx);
        return true;
    });

    //移除indexinfo
    m_pIndexInfoBuf[dataID] = -1;
    //FlushViewOfFile(&m_pIndexInfoBuf[dataID], sizeof(int));
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

    int headBlockIdx = m_pIndexInfoBuf[dataID];
    if (headBlockIdx < 0 || headBlockIdx >= m_blockCount)
        return false;

    int blockIdx = headBlockIdx;
    for (int i = 0; i < m_blockCount && blockIdx > -1; i++)
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

int SHM::getNoUsedBlockIdx()
{
	int idxRet = -1;
	do
	{
        DWORD n = -1;
        for (int i = 0; i < m_noUsedIdxWarehouseBufSize; i++)
        {
            //int n = getLowestNoZeroBitIndex(m_pNoUsedIdxWarehouseBuf[i]);
            //if (n != -1)
            //{
            //    idxRet = n + (i * 63);
            //    break;
            //}

#if defined(_M_X64) || defined(_M_AMD64)
            //返回非0的lowest位序号
			if (_BitScanForward64(&n, m_pNoUsedIdxWarehouseBuf[i]))
			{
				idxRet = n + (i * 64);
                break;
			}
#else
            if (_BitScanForward(&n, (DWORD)(m_pNoUsedIdxWarehouseBuf[i])))
            {
                idxRet = n + (i * 64);
                break;
            }
			else if (_BitScanForward(&n, (DWORD)(m_pNoUsedIdxWarehouseBuf[i] >> 32)))
            {
                idxRet = (n + 32) + (i * 64);
                break;
            }
#endif
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

    setBit0(m_pNoUsedIdxWarehouseBuf[warehouse], idxInAWarehouse);
    //FlushViewOfFile(&m_pNoUsedIdxWarehouseBuf[warehouse], sizeof(unsigned __int64));

    return true;
}

bool SHM::setBlockIndexNoUsed(int blockIdx)
{
    int warehouse = -1;
    int idxInAWarehouse = -1;
    if (!whereInWarehouse(blockIdx, &warehouse, &idxInAWarehouse))
        return false;

    setBit1(m_pNoUsedIdxWarehouseBuf[warehouse], idxInAWarehouse);
    //FlushViewOfFile(&m_pNoUsedIdxWarehouseBuf[warehouse], sizeof(unsigned __int64));

    return true;
}

bool SHM::whereInWarehouse(int blockIdx, int* warehouseIdx, int* idxInAWarehouse)
{
    if (blockIdx < 0 || blockIdx >= m_blockCount)
        return false;

	//找到仓库的索引
    *warehouseIdx = blockIdx / 64;
    if (*warehouseIdx >= m_noUsedIdxWarehouseBufSize)
        return false;

	//找到在仓库中的索引
    *idxInAWarehouse = blockIdx % 64;

    return true;
}
