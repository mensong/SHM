#include "shm.h"
#include <set>


SHM::SHM()
    : m_hMapFile(NULL)
    , m_pBuf(NULL)
	, m_pIndexBuf(NULL)
	, m_pBlockBuf(NULL)
    , m_indexBufSize(0)
    , m_pNoUsedIdxWarehouseBuf(NULL)
	, m_noUsedIdxWarehouseBufSize(0)
    , m_blockCount(100)
    , m_blockSize(512)
    , m_lastUsedIdx(-1)
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
        m_pIndexBuf = NULL;
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

    //��������Ŀռ�
    m_indexBufSize = 
        m_blockCount * 3 + //-1 dataID 0 1 2 -1 3 -1 4 5 -1 -1
        2;                //�������-1 -1

	//�����ֿ�����ռ�Ŀ��������ٸ�int64����
    // ʹ�ö��int64���洢��ÿ��int64�洢64��bit��ÿ��bit����һ��block���������Ƿ�ʹ�ã�ʹ��������Ϊ0��δʹ������Ϊ1��
	// Ȼ��ʹ�� num & (-num) ����ȡ���λ��1������ȡδʹ�õ������š�
    m_noUsedIdxWarehouseBufSize = m_blockCount / 63;//һ��int64�洢63��bit�����һ��bit���á�
    if (m_blockCount % 63 != 0)
        m_noUsedIdxWarehouseBufSize++;

    LARGE_INTEGER allocSize;
    allocSize.QuadPart =
        m_indexBufSize * sizeof(int) + //block�����ſռ��С
        m_noUsedIdxWarehouseBufSize * sizeof(__int64) + //blockδʹ�õ������ſռ��С
        blockCount * sizeof(int) + (blockCount * (blockSize * sizeof(char))); //�洢���ݴ�С�Ŀռ� + block�����ݿռ��С

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

	m_pIndexBuf = (int*)m_pBuf;
    for (int i = 0; i < m_indexBufSize; i++)
        m_pIndexBuf[i] = -1;

	m_pNoUsedIdxWarehouseBuf = (__int64*)(m_pIndexBuf + m_indexBufSize);
    for (int i = 0; i < m_noUsedIdxWarehouseBufSize; i++)
        m_pNoUsedIdxWarehouseBuf[i] = 0b0111111111111111111111111111111111111111111111111111111111111111;

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

    if (!m_pIndexBuf || !m_pBlockBuf)
    {
        m_mutex.Unlock();
        return false;
    }

    std::map<int, std::vector<int>> allIndexs;
    getAllIndeies(allIndexs);
    allIndexs.erase(dataID);

    const char* pDataToWrite = pData;
    int writeSize = dataSize;

    //д����һ��block������
    while (writeSize > m_blockSize)
    {
        int newIdx = getNoUsedIdx();
        if (newIdx < 0)
            return false;

        char* p = m_pBlockBuf + (newIdx * (sizeof(int) + m_blockSize));
        memcpy(p, (const void*)&m_blockSize, sizeof(int));
        memcpy(p + sizeof(int), pDataToWrite, m_blockSize);
        allIndexs[dataID].push_back(newIdx);

        writeSize -= m_blockSize;
        pDataToWrite += m_blockSize;
        m_lastUsedIdx = newIdx;
		setBlockIndexUsed(newIdx);
    }
    //дС��һ��block������
    if (writeSize > 0)
    {
        int newIdx = getNoUsedIdx();
        if (newIdx < 0)
            return false;

        char* p = m_pBlockBuf + (newIdx * (sizeof(int) + m_blockSize));
        memcpy(p, (const void*)&writeSize, sizeof(int));
        memcpy(p + sizeof(int), pDataToWrite, writeSize);
        allIndexs[dataID].push_back(newIdx);

        m_lastUsedIdx = newIdx;
        setBlockIndexUsed(newIdx);
    }

    //��дindexes data
    int w = 0;
    for (auto it = allIndexs.begin(); it != allIndexs.end(); ++it)
    {
        //-1 dataID
        m_pIndexBuf[w++] = -1;
        m_pIndexBuf[w++] = it->first;

        auto& indexs = it->second;
        for (size_t i = 0; i < indexs.size(); i++)
        {
            m_pIndexBuf[w++] = indexs[i];
        }
    }
    //������ -1 -1
    m_pIndexBuf[w++] = -1;
    m_pIndexBuf[w++] = -1;

    m_mutex.Unlock();
    return true;
}

int SHM::Read(char* pOutBuf, int outBufSize, int dataID)
{
    if (dataID >= m_blockCount)
        return -1;

    m_mutex.Lock();

    if (!m_pIndexBuf || !m_pBlockBuf)
    {
        m_mutex.Unlock();
        return -1;
    }

    std::vector<int> indexs;
    getIndeies(dataID, indexs);

    int dataSizeTotal = 0;
    for (size_t i = 0; i < indexs.size(); i++)
    {
        char* p = m_pBlockBuf + ((indexs[i]) * (sizeof(int) + m_blockSize));

        int dataSize = 0;
        memcpy((PVOID)&dataSize, p, sizeof(int));

        if (pOutBuf && outBufSize > 0)
        {
			int readSize = min(dataSize, outBufSize);
			memcpy(pOutBuf + dataSizeTotal, p + sizeof(int), readSize);

			if (outBufSize <= dataSize)
			{//�Ѷ���
				pOutBuf += readSize;
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

    if (!m_pIndexBuf)
    {
        m_mutex.Unlock();
        return false;
    }
    
	std::map<int, std::vector<int>> allIndexs;
	getAllIndeies(allIndexs);

    //
    auto itErase = allIndexs.find(dataID);
    if (itErase == allIndexs.end())
        return false;
    
	//��������Ϊδʹ��
	for (size_t i = 0; i < itErase->second.size(); i++)
	{
		setBlockIndexNoUsed(itErase->second[i]);
	}

    //ֱ��ɾ������
	allIndexs.erase(itErase);

	//��дindexes data
	int w = 0;
	for (auto it = allIndexs.begin(); it != allIndexs.end(); ++it)
	{
		//-1 dataID
		m_pIndexBuf[w++] = -1;
		m_pIndexBuf[w++] = it->first;

		auto& indexs = it->second;
		for (size_t i = 0; i < indexs.size(); i++)
		{
			m_pIndexBuf[w++] = indexs[i];
		}
	}
	//������ -1 -1
	m_pIndexBuf[w++] = -1;
	m_pIndexBuf[w++] = -1;

	m_mutex.Unlock();
    return true;
}

void SHM::ListDataIDs(std::vector<int>& idxs)
{
    m_mutex.Lock();

    if (!m_pIndexBuf)
    {
        m_mutex.Unlock();
        return;
    }

    bool isLastFlag = false;//�ϴ��Ƿ���-1
    for (int i = 0; i < m_indexBufSize; i++)
    {
        int n = m_pIndexBuf[i];

        if (n == -1)
        {//����flag
            if (isLastFlag)
            {//������������-1�������
                return;
            }
            else //if (!isLastFlag)
            {//������ʼ��־
                isLastFlag = true;
            }
        }
        else //if (n != -1)
        {//������flag
            if (isLastFlag)
            {//�ϴ���һ��-1���������dataID
                idxs.push_back(n);
            }

            isLastFlag = false;
        }
    }

    m_mutex.Unlock();
}

void SHM::getIndeies(int dataID, std::vector<int>& idxs)
{
    if (!m_pIndexBuf)
        return;

	bool isLastFlag = false;//�ϴ��Ƿ���-1
    int idx = 0;
    bool isInValidDataID = false;
	for (int i = 0; i < m_indexBufSize; i++)
	{
		int n = m_pIndexBuf[i];

		if (n == -1)
		{//����flag
			if (isLastFlag)
			{//������������-1�������
				return;
			}
			else //if (!isLastFlag)
			{//������ʼ��־
				isLastFlag = true;

                if (isInValidDataID)
                {//�Ѿ���ȡ���ˣ�����Ҫ�����¶�ȡ
                    return;
                }
			}
		}
		else //if (n != -1)
		{//������flag
			if (isLastFlag)
			{//�ϴ���һ��-1���������dataID
                if (n == dataID)
                {
                    isInValidDataID = true;
                }
			}
			else if (isInValidDataID)
			{//����Ч����������
                idxs.push_back(n);
			}

			isLastFlag = false;
		}
	}
}

void SHM::getAllIndeies(std::map<int, std::vector<int>>& idxs)
{
    if (!m_pIndexBuf)
        return;

    int dataID = -1;//��ǰ��dataID
    bool isLastFlag = false;//�ϴ��Ƿ���-1
    for (int i = 0; i < m_indexBufSize; i++)
    {
        int n = m_pIndexBuf[i];

        if (n == -1)
        {//����flag
            if (isLastFlag)
            {//������������-1�������
                return;
            }
            else //if (!isLastFlag)
			{//������ʼ��־
				isLastFlag = true;
				dataID = -1;
            }
        }
        else //if (n != -1)
        {//������flag
            if (isLastFlag)
            {//�ϴ���һ��-1���������dataID
                dataID = n;
            }
            else if (dataID != -1)
            {//����������
                idxs[dataID].push_back(n);
            }

            isLastFlag = false;
        }
    }
}

int SHM::getNoUsedIdx()
{
#if 0
    DWORD t = ::GetTickCount();

	int idxRet = -1;
    do
    {
        std::set<int> _usedIdxs;
        for (auto it = usedIdxs.begin(); it != usedIdxs.end(); ++it)
        {
            for (size_t i = 0; i < it->second.size(); i++)
            {
                _usedIdxs.insert(it->second[i]);
            }
        }

        int idxSpaceSize = m_blockCount;
        for (size_t i = startIdx; i < idxSpaceSize; i++)
        {
            if (_usedIdxs.find(i) == _usedIdxs.end())
            {
                idxRet = i;
                break;
            }
        }

        for (size_t i = 0; i < startIdx; i++)
        {
            if (_usedIdxs.find(i) == _usedIdxs.end())
            {
                idxRet = i;
                break;
            }
        }
    } while (false);
    
	times += ::GetTickCount() - t;

    return idxRet;

#else

	int idxRet = -1;
	do
	{
        for (int i = 0; i < m_noUsedIdxWarehouseBufSize; i++)
        {
            __int64 lowest = m_pNoUsedIdxWarehouseBuf[i] & (-m_pNoUsedIdxWarehouseBuf[i]);
            int n = getNoZeroBitNum(lowest);
			if (n != 0)
			{
				idxRet = (n - 1) + (i * 63);
                break;
			}

        }
	} while (false);
	return idxRet;

#endif
}

int SHM::getNoZeroBitNum(__int64 warehouse)
{
	switch (warehouse)
	{
	case 0b0000000000000000000000000000000000000000000000000000000000000001:
		return 1;
    case 0b0000000000000000000000000000000000000000000000000000000000000010:
        return 2;
    case 0b0000000000000000000000000000000000000000000000000000000000000100:
        return 3;
    case 0b0000000000000000000000000000000000000000000000000000000000001000:
        return 4;
    case 0b0000000000000000000000000000000000000000000000000000000000010000:
        return 5;
    case 0b0000000000000000000000000000000000000000000000000000000000100000:
        return 6;
    case 0b0000000000000000000000000000000000000000000000000000000001000000:
        return 7;
    case 0b0000000000000000000000000000000000000000000000000000000010000000:
        return 8;
    case 0b0000000000000000000000000000000000000000000000000000000100000000:
        return 9;
    case 0b0000000000000000000000000000000000000000000000000000001000000000:
        return 10;
    case 0b0000000000000000000000000000000000000000000000000000010000000000:
        return 11;
    case 0b0000000000000000000000000000000000000000000000000000100000000000:
        return 12;
    case 0b0000000000000000000000000000000000000000000000000001000000000000:
        return 13;
    case 0b0000000000000000000000000000000000000000000000000010000000000000:
        return 14;
    case 0b0000000000000000000000000000000000000000000000000100000000000000:
        return 15;
    case 0b0000000000000000000000000000000000000000000000001000000000000000:
        return 16;
    case 0b0000000000000000000000000000000000000000000000010000000000000000:
        return 17;
    case 0b0000000000000000000000000000000000000000000000100000000000000000:
        return 18;
    case 0b0000000000000000000000000000000000000000000001000000000000000000:
        return 19;
    case 0b0000000000000000000000000000000000000000000010000000000000000000:
        return 20;
    case 0b0000000000000000000000000000000000000000000100000000000000000000:
        return 21;
    case 0b0000000000000000000000000000000000000000001000000000000000000000:
        return 22;
    case 0b0000000000000000000000000000000000000000010000000000000000000000:
        return 23;
    case 0b0000000000000000000000000000000000000000100000000000000000000000:
        return 24;
    case 0b0000000000000000000000000000000000000001000000000000000000000000:
        return 25;
    case 0b0000000000000000000000000000000000000010000000000000000000000000:
        return 26;
    case 0b0000000000000000000000000000000000000100000000000000000000000000:
        return 27;
    case 0b0000000000000000000000000000000000001000000000000000000000000000:
        return 28;
    case 0b0000000000000000000000000000000000010000000000000000000000000000:
        return 29;
    case 0b0000000000000000000000000000000000100000000000000000000000000000:
        return 30;
    case 0b0000000000000000000000000000000001000000000000000000000000000000:
        return 31;
    case 0b0000000000000000000000000000000010000000000000000000000000000000:
        return 32;
    case 0b0000000000000000000000000000000100000000000000000000000000000000:
        return 33;
    case 0b0000000000000000000000000000001000000000000000000000000000000000:
        return 34;
    case 0b0000000000000000000000000000010000000000000000000000000000000000:
        return 35;
    case 0b0000000000000000000000000000100000000000000000000000000000000000:
        return 36;
    case 0b0000000000000000000000000001000000000000000000000000000000000000:
        return 37;
    case 0b0000000000000000000000000010000000000000000000000000000000000000:
        return 38;
    case 0b0000000000000000000000000100000000000000000000000000000000000000:
        return 39;
    case 0b0000000000000000000000001000000000000000000000000000000000000000:
        return 40;
    case 0b0000000000000000000000010000000000000000000000000000000000000000:
        return 41;
    case 0b0000000000000000000000100000000000000000000000000000000000000000:
        return 42;
    case 0b0000000000000000000001000000000000000000000000000000000000000000:
        return 43;
    case 0b0000000000000000000010000000000000000000000000000000000000000000:
        return 44;
    case 0b0000000000000000000100000000000000000000000000000000000000000000:
        return 45;
    case 0b0000000000000000001000000000000000000000000000000000000000000000:
        return 46;
    case 0b0000000000000000010000000000000000000000000000000000000000000000:
        return 47;
    case 0b0000000000000000100000000000000000000000000000000000000000000000:
        return 48;
    case 0b0000000000000001000000000000000000000000000000000000000000000000:
        return 49;
    case 0b0000000000000010000000000000000000000000000000000000000000000000:
        return 50;
    case 0b0000000000000100000000000000000000000000000000000000000000000000:
        return 51;
    case 0b0000000000001000000000000000000000000000000000000000000000000000:
        return 52;
    case 0b0000000000010000000000000000000000000000000000000000000000000000:
        return 53;
    case 0b0000000000100000000000000000000000000000000000000000000000000000:
        return 54;
    case 0b0000000001000000000000000000000000000000000000000000000000000000:
        return 55;
    case 0b0000000010000000000000000000000000000000000000000000000000000000:
        return 56;
    case 0b0000000100000000000000000000000000000000000000000000000000000000:
        return 57;
    case 0b0000001000000000000000000000000000000000000000000000000000000000:
        return 58;
    case 0b0000010000000000000000000000000000000000000000000000000000000000:
        return 59;
    case 0b0000100000000000000000000000000000000000000000000000000000000000:
        return 60;
    case 0b0001000000000000000000000000000000000000000000000000000000000000:
        return 61;
    case 0b0010000000000000000000000000000000000000000000000000000000000000:
        return 62;
    case 0b0100000000000000000000000000000000000000000000000000000000000000:
        return 63;
    //case 0b1000000000000000000000000000000000000000000000000000000000000000:
	//    return 64;//���һ��bit����������־����ʹ��
	default:
		break;
	}
    return 0;
}

__int64 setBit0(__int64 num, int pos) 
{
    return num & ~(1ULL << pos);
}
__int64 setBit1(__int64 num, int pos) 
{
    return num | (1ULL << pos);
}


bool SHM::setBlockIndexUsed(int idx)
{
    if (idx < 0 || idx >= m_blockCount)
        return false;

	int warehouse = idx / 63;
    if (warehouse >= m_noUsedIdxWarehouseBufSize)
		return false;

    int idxInAWarehouse = idx % 63;
    m_pNoUsedIdxWarehouseBuf[warehouse] = 
        setBit0(m_pNoUsedIdxWarehouseBuf[warehouse], idxInAWarehouse);

    return true;
}

bool SHM::setBlockIndexNoUsed(int idx)
{
    if (idx < 0 || idx >= m_blockCount)
        return false;

    int warehouse = idx / 63;
    if (warehouse >= m_noUsedIdxWarehouseBufSize)
        return false;

    int idxInAWarehouse = idx % 63;
    m_pNoUsedIdxWarehouseBuf[warehouse] =
        setBit1(m_pNoUsedIdxWarehouseBuf[warehouse], idxInAWarehouse);

    return true;
}
