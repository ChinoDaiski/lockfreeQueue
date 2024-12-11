#pragma once

#include "MemoryPool.h"

template <typename T>
class lockfree_queue {
private:
    struct Node {
        T data;
        Node* next;    // ������ Node* next ���µ�, ��¥�� stamp���� �ھƼ� ���� 17bit�� ä�� ���� ������ ���� ���̱⿡
                        // bit ������ ���̱� ���ؼ� �̷��� ����Ѵ�.

        Node(T _data = 0, UINT64 _next = 0) : data{ _data }, next{ reinterpret_cast<Node*>(&_next)} {}
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
                    CAS(&_tail, tail, node); //<< ������ ��� �� ���� ����
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
    UINT64 top; // Top�� ��Ÿ���� tagged pointer
    UINT64 stamp; // ������ �Ǵ� stamp ��

    MemoryPool<Node, false> pool;

private:
    UINT32 _size;

    Node* _head;
    Node* _tail;
};
