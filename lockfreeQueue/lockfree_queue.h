#pragma once

#include "MemoryPool.h"
#include "CircularQueue.h"

extern void handler(void);

enum class ACTION : UINT32
{
    // enq ����
    ENQ_START,
    ENQ_CHECK_NULL,
    ENQ_TRY_CAS_ONE_BEFORE,
    ENQ_TRY_CAS_ONE,
    ENQ_TRY_CAS_TWO_BEFORE,
    ENQ_TRY_CAS_TWO,
    ENQ_TAIL_NEXT_NOT_NULL,

    // deq ����
    DEQ_START,
    DEQ_EMPTY,
    DEQ_FAIL_NO_VALUE,
    DEQ_TRY_NULL_CHECK,
    DEQ_TRY_CAS_BEFORE,
    DEQ_TRY_CAS,
    DEQ_SUCCESS_CAS,

    Q_SIZE = 100,

    END
};

typedef struct _tagDebugNode {
    UINT64 pNode;        // ���� ��� ������
    UINT64 pExpected;    // ���� ���� ��� �����ͷ� ���Ǵ� ������
    UINT64 pNext;        // ���� ��� ������
    ACTION action;          // ���� ���� (ENQ, DEQ)
    DWORD threadId;         // ������ ID
    bool casSuccess;        // CAS ���� ����

    _tagDebugNode() {}
    _tagDebugNode(UINT64 a, UINT64 b, UINT64 c, ACTION _action, DWORD _threadId, bool _success)
        : pNode{ a }, pExpected{ b }, pNext{ c }, action{ _action }, threadId{ _threadId }, casSuccess{ _success } {}
} DebugNode, * PDebugNode;


thread_local CircularQueue<DebugNode> thread_debugQueue{ 1000 };

template <typename T>
class lockfree_queue {
private:
    struct Node {
        T data;
        UINT64 next;    // ������ Node* next ���µ�, ��¥�� stamp���� �ھƼ� ���� 17bit�� ä�� ���� ������ ���� ���̱⿡
                        // bit ������ ���̱� ���ؼ� �̷��� ����Ѵ�.

        Node(T _data = 0, UINT64 _next = 0) : data{ _data }, next{ 0 } {}
    };

public:
    struct AddressConverter {
        static constexpr UINT64 POINTER_MASK = 0x00007FFFFFFFFFFF; // ���� 47��Ʈ
        static constexpr UINT64 STAMP_SHIFT = 47;

        // ��� �ּҿ� stamp �߰�
        static UINT64 AddStamp(Node* node, UINT64 stamp) {
            UINT64 pointerValue = reinterpret_cast<UINT64>(node);
            return (pointerValue & POINTER_MASK) | (stamp << STAMP_SHIFT);
        }

        // ��� �ּ� ����
        static Node* ExtractNode(UINT64 taggedPointer) {
            return reinterpret_cast<Node*>(taggedPointer & POINTER_MASK);
        }
    };

    //bool CAS(UINT64* target, UINT64 expected, UINT64 desired) {
    //    return InterlockedCompareExchange64(reinterpret_cast<LONG64*>(target), desired, expected) == expected;
    //}

private:
    // CAS �Լ� ����
    bool CAS(Node** target, Node* old_value, Node* new_value) {
        // �����͸� 64��Ʈ ������ ��ȯ
        auto result = InterlockedCompareExchange64(
            reinterpret_cast<volatile LONG64*>(target),  // ��� ������
            reinterpret_cast<LONG64>(new_value),        // �� ��
            reinterpret_cast<LONG64>(old_value)         // ���� ���� ��
        );

        // ��ȯ�� ���� old_value�� ������ Ȯ��
        return result == reinterpret_cast<LONG64>(old_value);
    }


    bool CAS(UINT64* target, UINT64 expected, UINT64 desired, UINT64& returnValue) {
        returnValue = InterlockedCompareExchange64(reinterpret_cast<LONG64*>(target), desired, expected);
        return returnValue == expected;
    }


public:
    lockfree_queue() : top(0), stamp(0) {
        _size = 0;
        Node* pNode = new Node;
        pNode->next = NULL;
        _head = AddressConverter::AddStamp(pNode, stamp);
        _tail = _head;

        bError = 1;
    }

    ~lockfree_queue() {
        DebugBreak();

        T value;
        while (dequeue(value));
    }

    void enqueue(const T& value)
    {
        if (InterlockedCompareExchange(&bError, 0, 0) == 0)
            return;

        DWORD curThreadID = GetCurrentThreadId();

        Node* node = pool.Alloc();
        node->data = value;
        node->next = 0;


        UINT64 stValue = InterlockedIncrement(&stamp);
        UINT64 newNode = AddressConverter::AddStamp(node, stValue);

        bool bSuccess = false;


        logQueue.enqueue(DebugNode{
            newNode,
            NULL,
            node->next,
            ACTION::ENQ_START,
            curThreadID,
            FALSE,
            }
        );

        thread_debugQueue.enqueue(DebugNode{
            newNode,
            NULL,
            node->next,
            ACTION::ENQ_START,
            curThreadID,
            FALSE,
            }
        );

        UINT32 bRepeat = 0;

        while (true)
        {
            // ���� �����κ� ĸ��
            UINT64 tail = _tail;

            // ���� ���� �κ��� ��� ����
            Node* tailNode = AddressConverter::ExtractNode(tail);
            UINT64 tailNext = tailNode->next;
            Node* tailNextNode = AddressConverter::ExtractNode(tailNext);


            logQueue.enqueue(DebugNode{
                 tail,
                 NULL,
                 tailNext,
                 ACTION::ENQ_CHECK_NULL,
                 curThreadID,
                 FALSE,
                }
            );

            thread_debugQueue.enqueue(DebugNode{
                 tail,
                 NULL,
                 tailNext,
                ACTION::ENQ_CHECK_NULL,
                curThreadID,
                FALSE,
                }
            );

            // �켱 ���� ��尡 ��� �ִ�, ���� ������ ��¥ �������� Ȯ��. �� ���� �ٸ� �����尡 �߰����� �� �� ������
            if (tailNextNode == NULL)
            {
                logQueue.enqueue(DebugNode{
                    tailNext,
                    NULL,
                    newNode,
                    ACTION::ENQ_TRY_CAS_ONE_BEFORE,
                    curThreadID,
                    FALSE,
                    }
                );

                thread_debugQueue.enqueue(DebugNode{
                    tailNext,
                    NULL,
                    newNode,
                    ACTION::ENQ_TRY_CAS_ONE_BEFORE,
                    curThreadID,
                    FALSE,
                    }
                );


                //======================================================================
                // 1�� CAS
                //======================================================================
                // tail�� next�� null�� ��� ���� �߰��� ��带 tail�� next�� ����
                // �� ���� ĸ�ĵ� tailNode->next ���� retval
                UINT64 retval = InterlockedCompareExchange64(reinterpret_cast<LONG64*>(&tailNode->next), newNode, NULL);
                if (retval == NULL)
                    bSuccess = true;
                else
                    bSuccess = false;

                //bSuccess = CAS(&tailNode->next, NULL, newNode);
                
                logQueue.enqueue(DebugNode{ 
                    retval,
                    NULL,
                    newNode,
                    ACTION::ENQ_TRY_CAS_ONE,
                    curThreadID,
                    bSuccess,
                    }
                );

                thread_debugQueue.enqueue(DebugNode{
                    retval,
                    NULL,
                    newNode,
                    ACTION::ENQ_TRY_CAS_ONE,
                    curThreadID,
                    bSuccess,
                    }
                );
                
                if (InterlockedCompareExchange(&bError, 0, 0) == 0)
                    return;

                // 1�� CAS ������
                if (bSuccess)
                {
                    logQueue.enqueue(DebugNode{
                        _tail,
                        tail,
                        newNode,
                        ACTION::ENQ_TRY_CAS_TWO_BEFORE,
                        curThreadID,
                        bSuccess,
                        }
                    );

                    thread_debugQueue.enqueue(DebugNode{
                        _tail,
                        tail,
                        newNode,
                        ACTION::ENQ_TRY_CAS_TWO_BEFORE,
                        curThreadID,
                        bSuccess,
                        }
                    );

                    if (InterlockedCompareExchange(&bError, 0, 0) == 0)
                        return;


                    UINT64 retval = InterlockedCompareExchange64(reinterpret_cast<LONG64*>(&_tail), newNode, tail);
                    if (retval == tail)
                        bSuccess = true;
                    else
                        bSuccess = false;

                    // ���� �ڷᱸ���� tail�� ���� �߰��� ��带 ����Ű���� ��.
                    //bSuccess = CAS(&_tail, tail, newNode); // << ������ ��� �� ���� ����

                    logQueue.enqueue(DebugNode{
                        retval,
                        tail,
                        newNode,
                        ACTION::ENQ_TRY_CAS_TWO,
                        curThreadID,
                        bSuccess,
                        }
                    );

                    thread_debugQueue.enqueue(DebugNode{
                        retval,
                        tail,
                        newNode,
                        ACTION::ENQ_TRY_CAS_TWO,
                        curThreadID,
                        bSuccess,
                        }
                    );
                    
                    if (InterlockedCompareExchange(&bError, 0, 0) == 0)
                        return;

                    break;
                }
            }
            else
            {
                // �̹� tail�� next node�� NULL�� �ƴ�. �׷��� _tail���� �������� �Űܾ���.
                //CAS(&_tail, tail, tailNext);

                UINT64 retval = InterlockedCompareExchange64(reinterpret_cast<LONG64*>(&_tail), tailNext, tail);
                if (retval == tail)
                    bSuccess = true;
                else
                    bSuccess = false;

                logQueue.enqueue(DebugNode
                    {
                        retval,
                        tail,
                        tailNext,
                        ACTION::ENQ_TAIL_NEXT_NOT_NULL,
                        curThreadID,
                        bSuccess,
                    }
                );

                thread_debugQueue.enqueue(DebugNode{
                    retval,
                    tail,
                    tailNext,
                    ACTION::ENQ_TAIL_NEXT_NOT_NULL,
                    curThreadID,
                    bSuccess,
                    });
            }

            UINT32 index = InterlockedIncrement(&bRepeat);

            if (maxRepeat < index)
                maxRepeat = index;

            if (bRepeat >= 1000)
            {
                logQueue.Stop();
                handler();
                exit(-1);
            }
        }

        //Q_SIZE
        long q_size = InterlockedIncrement(&_size);
        logQueue.enqueue(
            DebugNode{
                        0,
                        0,
                        0,
                        (ACTION)((UINT32)ACTION::Q_SIZE + q_size),
                        curThreadID,
                        FALSE,
            }
        );

        thread_debugQueue.enqueue(
            DebugNode{
                        0,
                        0,
                        0,
                        (ACTION)((UINT32)ACTION::Q_SIZE + q_size),
                        curThreadID,
                        FALSE,
            }
        );
    }

    bool dequeue(T& value) {
        if (InterlockedCompareExchange(&bError, 0, 0) == 0)
            return false;

        DWORD curThreadID = GetCurrentThreadId();
        bool bSuccess = false;

        logQueue.enqueue(DebugNode{
            0,
            0,
            0,
            ACTION::DEQ_START,
            curThreadID,
            FALSE,
            }
        );

        thread_debugQueue.enqueue(DebugNode{
            0,
            0,
            0,
            ACTION::DEQ_START,
            curThreadID,
            FALSE,
            }
        );

        //UINT32 qSize = InterlockedCompareExchange(&_size, 0, 0);

        // �ϴ� deq �Ϸ� ���Դٴ� ���� _size�� ���ҽ�Ű�� �Դ� �ǹ�. �׷��Ƿ� �ϴ� 1 ���ҽ�Ű�� �Ǻ�
        long qSize = InterlockedDecrement(&_size);

        // ���� ���ҵ� _size�� 0���� �۴ٸ� ����ٴ� �ǹ�
        if (qSize < 0)
        {
            logQueue.enqueue(DebugNode{
                0,
                0,
                0,
                ACTION::DEQ_FAIL_NO_VALUE,
                curThreadID,
                FALSE,
                }
            );

            thread_debugQueue.enqueue(DebugNode{
                0,
                0,
                0,
                ACTION::DEQ_FAIL_NO_VALUE,
                curThreadID,
                FALSE,
                }
            );

            InterlockedExchange(&bError, 0);

            // 1 ������Ŵ.
            InterlockedIncrement(&_size);
            return FALSE;
        }


        while (true)
        {
            UINT64 tail = _tail;         // tail �� ������

            // ���� ���� �κ��� ��� ����
            Node* tailNode = AddressConverter::ExtractNode(tail);
            UINT64 tailNext = tailNode->next;
            Node* tailNextNode = AddressConverter::ExtractNode(tailNext);

            // if tailNext�� 0�� �ƴ϶�� �ű�.
            if (tailNextNode != NULL)
            {
                UINT64 retval = InterlockedCompareExchange64(reinterpret_cast<LONG64*>(&_tail), tailNext, tail);
                continue;
            }




            UINT64 head = _head;         // ��� �� ������

            Node* headNode = AddressConverter::ExtractNode(head);

            // next���� UINT64
            UINT64 headNext = headNode->next;    // ����� next �� ������
            Node* headNextNode = AddressConverter::ExtractNode(headNext);
            
            logQueue.enqueue(DebugNode{
                head,
                headNext,
                0,
                ACTION::DEQ_TRY_NULL_CHECK,
                curThreadID,
                FALSE,
                }
            );

            thread_debugQueue.enqueue(DebugNode{
                head,
                headNext,
                0,
                ACTION::DEQ_TRY_NULL_CHECK,
                curThreadID,
                FALSE,
                }
            );

            if (headNextNode == NULL)   // next�� null��
            {
                logQueue.enqueue(DebugNode{
                    head,
                    0,
                    headNext,
                    ACTION::DEQ_EMPTY,
                    curThreadID,
                    FALSE,
                    }
                );

                thread_debugQueue.enqueue(DebugNode{
                    head,
                    0,
                    headNext,
                    ACTION::DEQ_EMPTY,
                    curThreadID,
                    FALSE,
                    }
                );

                // ���� ��� ������ 0�� �ƴ϶��? ��õ�
                qSize = InterlockedCompareExchange(&_size, 0, 0);
                if (qSize >= 0)
                    continue;

                InterlockedExchange(&bError, 0);

                return FALSE;   // ��� �ִٴ� �ǹ̴ϱ� deq ����
            }
            else
            {
                logQueue.enqueue(DebugNode{
                        _head,
                        head,
                        headNext,
                        ACTION::DEQ_TRY_CAS_BEFORE,
                        curThreadID,
                        FALSE,
                    }
                );

                thread_debugQueue.enqueue(DebugNode{
                        _head,
                        head,
                        headNext,
                        ACTION::DEQ_TRY_CAS_BEFORE,
                        curThreadID,
                        FALSE,
                    }
                );

                // next�� null�� �ƴ϶� ���� ��尡 �����Ѵٴ� ���̴� ȹ�� �õ�

                //bSuccess = CAS(&_head, head, headNext); // head�� 

                UINT64 retval = InterlockedCompareExchange64(reinterpret_cast<LONG64*>(&_head), headNext, head);
                if (retval == head)
                    bSuccess = true;
                else
                    bSuccess = false;

                logQueue.enqueue(DebugNode{
                    retval,
                    head,
                    headNext,
                    ACTION::DEQ_TRY_CAS,
                    curThreadID,
                    bSuccess,
                    }
                );

                thread_debugQueue.enqueue(DebugNode{
                    retval,
                    head,
                    headNext,
                    ACTION::DEQ_TRY_CAS,
                    curThreadID,
                    bSuccess,
                    }
                );
                
                if (bSuccess)
                {
                    value = AddressConverter::ExtractNode(headNext)->data;
                    pool.Free(headNode);

                    logQueue.enqueue(DebugNode{
                        retval,
                        head,
                        headNext,
                        ACTION::DEQ_SUCCESS_CAS,
                        curThreadID,
                        bSuccess,
                        }
                    );

                    thread_debugQueue.enqueue(DebugNode{
                        retval,
                        head,
                        headNext,
                        ACTION::DEQ_SUCCESS_CAS,
                        curThreadID,
                        bSuccess,
                        }
                    );

                    break;
                }
            }
        }



        //Q_SIZE

        logQueue.enqueue(
            DebugNode{
                        0,
                        0,
                        0,
                        (ACTION)((UINT32)ACTION::Q_SIZE + qSize),
                        curThreadID,
                        FALSE,
            }
        );

        thread_debugQueue.enqueue(
            DebugNode{
                        0,
                        0,
                        0,
                        (ACTION)((UINT32)ACTION::Q_SIZE + qSize),
                        curThreadID,
                        FALSE,
            }
        );

        return TRUE;
    }


public:
    UINT32 GetCurPoolCount(void) { return pool.GetCurPoolCount(); }
    UINT32 GetMaxPoolCount(void) { return pool.GetMaxPoolCount(); }

public:
    CircularQueue<DebugNode> logQueue;  // ����� ���� ť

private:
    UINT64 top; // Top�� ��Ÿ���� tagged pointer
    UINT64 stamp; // ������ �Ǵ� stamp ��

    MemoryPool<Node, false> pool;

public:
    long _size;

    UINT64 _head;
    UINT64 _tail;

    UINT64 bError;

    UINT64 maxRepeat = 0;
};

