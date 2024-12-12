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


    bool CAS(UINT64* target, UINT64 expected, UINT64 desired) {
        return InterlockedCompareExchange64(reinterpret_cast<LONG64*>(target), desired, expected) == expected;
    }


public:
    lockfree_queue() : top(0), stamp(0) {
        _size = 0;
        Node* pNode = new Node;
        pNode->next = NULL;
        _head = AddressConverter::AddStamp(pNode, stamp);
        _tail = _head;
    }

    ~lockfree_queue() {
        DebugBreak();

        T value;
        while (dequeue(value));
    }

    void enqueue(const T& value)
    {
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
            Node* tailNodeNext = AddressConverter::ExtractNode(tailNext);

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
            if (tailNodeNext == NULL)
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

                // tail�� next�� null�� ��� ���� �߰��� ��带 tail�� next�� ����
                bSuccess = CAS(&tailNode->next, tailNext, newNode);

                logQueue.enqueue(DebugNode{ 
                    tailNode->next,
                    tailNext,
                    newNode,
                    ACTION::ENQ_TRY_CAS_ONE,
                    curThreadID,
                    bSuccess,
                    }
                );

                thread_debugQueue.enqueue(DebugNode{
                    tailNode->next,
                    tailNext,
                    newNode,
                    ACTION::ENQ_TRY_CAS_ONE,
                    curThreadID,
                    bSuccess,
                    }
                );

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

                    // ���� �ڷᱸ���� tail�� ���� �߰��� ��带 ����Ű���� ��.
                    bSuccess = CAS(&_tail, tail, newNode); //<< ������ ��� �� ���� ����

                    logQueue.enqueue(DebugNode{
                        _tail,
                        tail,
                        newNode,
                        ACTION::ENQ_TRY_CAS_TWO,
                        curThreadID,
                        bSuccess,
                        }
                    );

                    thread_debugQueue.enqueue(DebugNode{
                        _tail,
                        tail,
                        newNode,
                        ACTION::ENQ_TRY_CAS_TWO,
                        curThreadID,
                        bSuccess,
                        }
                    );

                    break;
                }
            }
            else
            {
                logQueue.enqueue(DebugNode
                    {
                        tail,
                        NULL,
                        reinterpret_cast<UINT64>(tailNodeNext),
                        ACTION::ENQ_TAIL_NEXT_NOT_NULL,
                        curThreadID,
                        FALSE,
                    }
                );

                thread_debugQueue.enqueue(DebugNode{
                    tail,
                    NULL,
                    reinterpret_cast<UINT64>(tailNodeNext),
                    ACTION::ENQ_TAIL_NEXT_NOT_NULL,
                    curThreadID,
                    FALSE,
                    });
            }


            UINT32 index = InterlockedIncrement(&bRepeat);

            if (bRepeat >= 1000)
            {
                logQueue.Stop();
                handler();
                exit(-1);
            }
        }
        //Q_SIZE
        UINT32 q_size = InterlockedIncrement(&_size);
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

        UINT32 qSize = InterlockedCompareExchange(&_size, 0, 0);

        if (qSize == 0)
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

            return FALSE;
        }

        while (true)
        {
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

                bSuccess = CAS(&_head, head, headNext); // head�� 

                logQueue.enqueue(DebugNode{
                    _head,
                    head,
                    headNext,
                    ACTION::DEQ_TRY_CAS,
                    curThreadID,
                    bSuccess,
                    }
                );

                thread_debugQueue.enqueue(DebugNode{
                    _head,
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
                        _head,
                        head,
                        headNext,
                        ACTION::DEQ_SUCCESS_CAS,
                        curThreadID,
                        bSuccess,
                        }
                    );

                    thread_debugQueue.enqueue(DebugNode{
                        _head,
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
        UINT32 q_size = InterlockedDecrement(&_size);

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

private:
    UINT32 _size;

    UINT64 _head;
    UINT64 _tail;
};
