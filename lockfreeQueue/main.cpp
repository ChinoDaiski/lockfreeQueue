
#include <iostream>
#include <windows.h>
#include <process.h>
#include <fstream>
#include <iomanip>

#include "lockfree_queue.h"


bool bExitWorker = false;
bool bExitMonitor = false;

// �۷ι� ���� �ν��Ͻ�
lockfree_queue<int> g_Queue;

UINT32 thread_cnt = -1;
CircularQueue<DebugNode>* g_circularQueue[10];
DWORD threadIDs[10];

CRITICAL_SECTION cs;






void handler(void)
{
    DWORD curThreadID = GetCurrentThreadId();

    // ���
    EnterCriticalSection(&cs);

    // ����� ���� ���
    DebugNode node;

    std::ofstream outFile{ "debug_error.txt" };

    if (!outFile)
        std::cerr << "���� ���� ����...\n";

    for (int i = 0; i < CQSIZE; ++i)
    {
        node = g_Queue.logQueue.dequeue();

        outFile << std::dec << std::setw(4) << std::setfill('0') << i + 1 << ". Action: ";

        switch (node.action)
        {
        case ACTION::ENQ_START:
            outFile << "ENQ_START___________";
            break;
        case ACTION::ENQ_CHECK_NULL:
            outFile << "ENQ_CHECK_NULL___";
            break;
        case ACTION::ENQ_TRY_CAS_ONE_BEFORE:
            outFile << "ENQ_TRY_CAS_ONE_BEFORE";
            break;
        case ACTION::ENQ_TRY_CAS_ONE:
            outFile << "ENQ_TRY_CAS_ONE__";
            break;
        case ACTION::ENQ_TRY_CAS_TWO_BEFORE:
            outFile << "ENQ_TRY_CAS_TWO_BEFORE";
            break;
        case ACTION::ENQ_TRY_CAS_TWO:
            outFile << "ENQ_TRY_CAS_TWO__";
            break;
        case ACTION::ENQ_TAIL_NEXT_NOT_NULL:
            outFile << "ENQ_TAIL_NEXT_NOT_NULL";
            break;
        case ACTION::DEQ_START:
            outFile << "DEQ_START__________";
            break;
        case ACTION::DEQ_EMPTY:
            outFile << "DEQ_EMPTY________";
            break;
        case ACTION::DEQ_TRY_NULL_CHECK:
            outFile << "DEQ_TRY_NULL_CHECK";
            break;
        case ACTION::DEQ_FAIL_NO_VALUE:
            outFile << "DEQ_FAIL_NO_VALUE";
            break; 
        case ACTION::DEQ_TRY_CAS_BEFORE:
            outFile << "DEQ_TRY_CAS_BEFORE";
            break;
        case ACTION::DEQ_TRY_CAS:
            outFile << "DEQ_TRY_CAS______";
            break;
        case ACTION::DEQ_SUCCESS_CAS:
            outFile << "DEQ_SUCCESS_CAS__";
            break;
        case ACTION::END:
            break;
        default:
            break;
        }

        if ((UINT32)(node.action) >= 90)
        {
            // ���� ����
            outFile << "Q_SIZE : " << ((UINT32)(node.action) - 100) << std::endl;
            continue;
        }


        outFile << "\tThread: " << std::dec << node.threadId
            << "\tCAS Success: " << (node.casSuccess ? "Yes" : "No")
            << "\tOld_____: " << std::hex << node.pNode;

        if (!node.pNode)
        {
            outFile << "\t\t";
        }

        outFile << "\tExpected: " << std::hex << node.pExpected;

        if (!node.pExpected)
        {
            outFile << "\t";
        }
        outFile << "\tNew_____: " << std::hex << node.pNext;

        if (!node.pNext)
        {
            outFile << "\t";
        }

        outFile << std::endl;
    }

    outFile << "Thread ID : " << std::dec << curThreadID << " occured errors" << std::endl << std::endl;


    // ������ 1000���� �α� ���
    
    
    for (int k = 0; k < thread_cnt + 1; ++k)
    {
        outFile << "Current Thread ID : " << std::dec << threadIDs[k] << std::endl << std::endl;

        for (int i = 0; i < 1000; ++i)
        {
            node = g_circularQueue[k]->dequeue();

            outFile << std::dec << std::setw(4) << std::setfill('0') << i + 1 << ". Action: ";

            switch (node.action)
            {
            case ACTION::ENQ_START:
                outFile << "ENQ_START___________";
                break;
            case ACTION::ENQ_CHECK_NULL:
                outFile << "ENQ_CHECK_NULL___";
                break;
            case ACTION::ENQ_TRY_CAS_ONE:
                outFile << "ENQ_TRY_CAS_ONE__";
                break;
            case ACTION::ENQ_TRY_CAS_TWO:
                outFile << "ENQ_TRY_CAS_TWO__";
                break;
            case ACTION::DEQ_START:
                outFile << "DEQ_START__________";
                break;
            case ACTION::DEQ_EMPTY:
                outFile << "DEQ_EMPTY________";
                break;
            case ACTION::DEQ_FAIL_NO_VALUE:
                outFile << "DEQ_FAIL_NO_VALUE";
                break;
            case ACTION::DEQ_TRY_CAS:
                outFile << "DEQ_TRY_CAS______";
                break;
            case ACTION::DEQ_SUCCESS_CAS:
                outFile << "DEQ_SUCCESS_CAS__";
                break;
            case ACTION::END:
                break;
            default:
                outFile << "\t\t";
                break;
            }

            if ((UINT32)(node.action) >= 90)
            {
                // ���� ����
                outFile << "Thread: " << std::dec << node.threadId << "Q_SIZE : " << ((UINT32)(node.action) - 100) << std::endl;
                continue;
            }


            outFile << "\tThread: " << std::dec << node.threadId
                << "\tCAS Success: " << (node.casSuccess ? "Yes" : "No")
                << "\tOld_____: " << std::hex << node.pNode;

            if (!node.pNode)
            {
                outFile << "\t\t";
            }

            outFile << "\tExpected: " << std::hex << node.pExpected;

            if (!node.pExpected)
            {
                outFile << "\t";
            }
            outFile << "\tNew_____: " << std::hex << node.pNext;

            if (!node.pNext)
            {
                outFile << "\t";
            }

            outFile << std::endl;
        }
    }

    
    



    outFile.close();

    exit(-1); 
}










// �۾��� ������ �Լ�
unsigned int WINAPI Worker1(void* pArg) {

    UINT32 threadCnt = InterlockedIncrement(&thread_cnt);
    g_circularQueue[threadCnt] = &thread_debugQueue;
    threadIDs[threadCnt] = GetCurrentThreadId();

    const int cnt = 5;// rand() % 1000;

    while (!bExitWorker) {
        for (int i = 0; i < cnt; ++i) {
            g_Queue.enqueue(i);
        }

        int value;
        for (int i = 0; i < cnt; ++i) {
            if (!g_Queue.dequeue(value))
            {
                //break;
                handler();
                DebugBreak();
                //throw std::exception();
            }
        }
    }

    return 0;
}
unsigned int WINAPI Worker2(void* pArg) {

    UINT32 threadCnt = InterlockedIncrement(&thread_cnt);
    g_circularQueue[threadCnt] = &thread_debugQueue;
    threadIDs[threadCnt] = GetCurrentThreadId();

    const int cnt = 3;// rand() % 1000;

    while (!bExitWorker) {
        for (int i = 0; i < cnt; ++i) {
            g_Queue.enqueue(i);
        }

        int value;
        for (int i = 0; i < cnt; ++i) {
            if (!g_Queue.dequeue(value))
            {
                //break;
                handler();
                DebugBreak();
                //throw std::exception();
            }
        }
    }

    return 0;
}



// ���ڷ� ���� ������ TPS �� ���� ���� ������ 1�ʸ��� �����ϴ� ������
unsigned int WINAPI MonitorThread(void* pArg)
{
    while (!bExitMonitor)
    {
        std::cout << "===================================\n";

        std::cout << "CurPoolCount : " << g_Queue.GetCurPoolCount() << "\n";
        std::cout << "MaxPoolCount : " << g_Queue.GetMaxPoolCount() << "\n";

        std::cout << "===================================\n\n";

        // 1�ʰ� Sleep
        Sleep(1000);
    }

    return 0;
}




int main(void)
{
    InitializeCriticalSection(&cs);

    const int ThreadCnt = 1;
    HANDLE hHandle[ThreadCnt + 1];

    for (int i = 1; i <= ThreadCnt; ++i) {
        hHandle[i] = (HANDLE)_beginthreadex(NULL, 0, Worker1, NULL, 0, NULL);
        hHandle[i] = (HANDLE)_beginthreadex(NULL, 0, Worker2, NULL, 0, NULL);
    }
    // ����� ������ ����
    hHandle[0] = (HANDLE)_beginthreadex(NULL, 0, MonitorThread, NULL, 0, NULL);

    WCHAR ControlKey;

    while (1)
    {
        ControlKey = _getwch();
        if (ControlKey == L'q' || ControlKey == L'Q')
        {
            bExitWorker = true;
            break;
        }
    }

    WaitForMultipleObjects(ThreadCnt, &hHandle[1], TRUE, INFINITE);

    bExitMonitor = true;

    WaitForSingleObject(hHandle[0], INFINITE);


    std::cout << "===================================\n";

    std::cout << "CurPoolCount : " << g_Queue.GetCurPoolCount() << "\n";
    std::cout << "MaxPoolCount : " << g_Queue.GetMaxPoolCount() << "\n";

    std::cout << "===================================\n\n";
    std::cout << "���μ��� ����" << "\n";

    DeleteCriticalSection(&cs);

    return 0;
}


