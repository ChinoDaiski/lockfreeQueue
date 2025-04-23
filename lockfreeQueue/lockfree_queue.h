#pragma once

#include "MemoryPool.h"
#include "CircularQueue.h"

extern void handler(void);

enum class ACTION : UINT32
{
    // enq 관련
    ENQ_START,
    ENQ_CHECK_NULL,
    ENQ_TRY_CAS_ONE_BEFORE,
    ENQ_TRY_CAS_ONE,
    ENQ_TRY_CAS_TWO_BEFORE,
    ENQ_TRY_CAS_TWO,
    ENQ_TAIL_NEXT_NOT_NULL,

    // deq 관련
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
    UINT64 pNode;        // 이전 노드 포인터
    UINT64 pExpected;    // 원래 이전 노드 포인터로 기대되는 포인터
    UINT64 pNext;        // 다음 노드 포인터
    ACTION action;          // 동작 유형 (ENQ, DEQ)
    DWORD threadId;         // 스레드 ID
    bool casSuccess;        // CAS 성공 여부

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
        UINT64 next;    // 원래는 Node* next 였는데, 어짜피 stamp값을 박아서 상위 17bit를 채운 값을 가지고 있을 것이기에
                        // bit 연산을 줄이기 위해서 이렇게 사용한다.

        Node(T _data = 0, UINT64 _next = 0) : data{ _data }, next{ 0 } {}
    };

public:
    struct AddressConverter {
        static constexpr UINT64 POINTER_MASK = 0x00007FFFFFFFFFFF; // 하위 47비트
        static constexpr UINT64 STAMP_SHIFT = 47;

        // 노드 주소에 stamp 추가
        static UINT64 AddStamp(Node* node, UINT64 stamp) {
            UINT64 pointerValue = reinterpret_cast<UINT64>(node);
            return (pointerValue & POINTER_MASK) | (stamp << STAMP_SHIFT);
        }

        // 노드 주소 추출
        static Node* ExtractNode(UINT64 taggedPointer) {
            return reinterpret_cast<Node*>(taggedPointer & POINTER_MASK);
        }
    };

    //bool CAS(UINT64* target, UINT64 expected, UINT64 desired) {
    //    return InterlockedCompareExchange64(reinterpret_cast<LONG64*>(target), desired, expected) == expected;
    //}

private:
    // CAS 함수 구현
    bool CAS(Node** target, Node* old_value, Node* new_value) {
        // 포인터를 64비트 정수로 변환
        auto result = InterlockedCompareExchange64(
            reinterpret_cast<volatile LONG64*>(target),  // 대상 포인터
            reinterpret_cast<LONG64>(new_value),        // 새 값
            reinterpret_cast<LONG64>(old_value)         // 비교할 기존 값
        );

        // 반환된 값이 old_value와 같은지 확인
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
            // 기존 꼬리부분 캡쳐
            UINT64 tail = _tail;

            // 기존 꼬리 부분의 노드 추출
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

            // 우선 다음 노드가 비어 있는, 현재 꼬리가 진짜 꼬리인지 확인. 그 사이 다른 스레드가 추가했을 수 도 있으니
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
                // 1번 CAS
                //======================================================================
                // tail의 next가 null일 경우 새로 추가한 노드를 tail의 next에 대입
                // 이 순간 캡쳐된 tailNode->next 값이 retval
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

                // 1번 CAS 성공시
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

                    // 이후 자료구조의 tail이 새로 추가된 노드를 가리키도록 함.
                    //bSuccess = CAS(&_tail, tail, newNode); // << 실패의 경우 그 이유 추적

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
                // 이미 tail의 next node가 NULL이 아님. 그래서 _tail값을 다음으로 옮겨야함.
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

        // 일단 deq 하러 들어왔다는 것은 _size를 감소시키러 왔단 의미. 그러므로 일단 1 감소시키고 판별
        long qSize = InterlockedDecrement(&_size);

        // 만약 감소된 _size가 0보다 작다면 비었다는 의미
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

            // 1 증가시킴.
            InterlockedIncrement(&_size);
            return FALSE;
        }


        while (true)
        {
            UINT64 tail = _tail;         // tail 값 가져옴

            // 기존 꼬리 부분의 노드 추출
            Node* tailNode = AddressConverter::ExtractNode(tail);
            UINT64 tailNext = tailNode->next;
            Node* tailNextNode = AddressConverter::ExtractNode(tailNext);

            // if tailNext가 0이 아니라면 옮김.
            if (tailNextNode != NULL)
            {
                UINT64 retval = InterlockedCompareExchange64(reinterpret_cast<LONG64*>(&_tail), tailNext, tail);
                continue;
            }




            UINT64 head = _head;         // 헤드 값 가져옴

            Node* headNode = AddressConverter::ExtractNode(head);

            // next값은 UINT64
            UINT64 headNext = headNode->next;    // 헤드의 next 값 가져옴
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

            if (headNextNode == NULL)   // next가 null임
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

                // 현재 노드 갯수가 0이 아니라면? 재시도
                qSize = InterlockedCompareExchange(&_size, 0, 0);
                if (qSize >= 0)
                    continue;

                InterlockedExchange(&bError, 0);

                return FALSE;   // 비어 있다는 의미니깐 deq 실패
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

                // next가 null이 아니란 것은 노드가 존재한다는 것이니 획득 시도

                //bSuccess = CAS(&_head, head, headNext); // head와 

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
    CircularQueue<DebugNode> logQueue;  // 디버깅 정보 큐

private:
    UINT64 top; // Top을 나타내는 tagged pointer
    UINT64 stamp; // 기준이 되는 stamp 값

    MemoryPool<Node, false> pool;

public:
    long _size;

    UINT64 _head;
    UINT64 _tail;

    UINT64 bError;

    UINT64 maxRepeat = 0;
};

