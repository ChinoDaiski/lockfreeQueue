#pragma once

#include "MemoryPool.h"

template <typename T>
class lockfree_queue {
private:
    struct Node {
        T data;
        Node* next;    // 원래는 Node* next 였는데, 어짜피 stamp값을 박아서 상위 17bit를 채운 값을 가지고 있을 것이기에
                        // bit 연산을 줄이기 위해서 이렇게 사용한다.

        Node(T _data = 0, UINT64 _next = 0) : data{ _data }, next{ reinterpret_cast<Node*>(&_next)} {}
    };

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




public:
    lockfree_queue() : top(0), stamp(0) {
        _size = 0;
        _head = new Node;
        _head->next = NULL;
        _tail = _head;
    }

    ~lockfree_queue() {
        T value;
        while (dequeue(value));
    }

    void enqueue(const T& value)
    {
        Node* node = pool.Alloc();
        node->data = value;
        node->next = NULL;

        while (true)
        {
            Node* tail = _tail;
            Node* next = tail->next;

            if (next == NULL)
            {
                if (CAS(&tail->next, NULL, node))
                {                                
                    CAS(&_tail, tail, node); //<< 실패의 경우 그 이유 추적
                    break;
                }
            }
        }

        InterlockedExchangeAdd(&_size, 1);
    }

    bool dequeue(T& value) {
        if (_size == 0)
            return FALSE;

        while (true)
        {
            Node* head = _head;
            Node* next = head->next;

            if (next == NULL)
            {
                return FALSE;
            }
            else
            {
                if (CAS(&_head, head, next))
                {
                    value = next->data;
                    pool.Free(head);
                    break;
                }
            }
        }

        InterlockedExchangeAdd(&_size, -1);
        return TRUE;
    }


public:
    UINT32 GetCurPoolCount(void) { return pool.GetCurPoolCount(); }
    UINT32 GetMaxPoolCount(void) { return pool.GetMaxPoolCount(); }

private:
    UINT64 top; // Top을 나타내는 tagged pointer
    UINT64 stamp; // 기준이 되는 stamp 값

    MemoryPool<Node, false> pool;

private:
    UINT32 _size;

    Node* _head;
    Node* _tail;
};
