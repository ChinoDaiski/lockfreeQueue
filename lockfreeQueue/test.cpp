#include <windows.h>
#include <process.h>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <chrono>
#include <vector>
#include <algorithm>
#include <numeric>
#include <atomic>
#include <tbb/concurrent_queue.h>
#include "lockfree_queue.h"

// --- ��ġ��ũ ���� ---
int test = 1;                // 1: lockfree_queue, 2: tbb::concurrent_queue
int numThreads = 4;          // ��Ŀ ������ ��
const int workBatch = 10000; // �� ��ġ�� enqueue/dequeue ���� Ƚ��
const int benchmarkDuration = 3; // ��ġ��ũ ���� �ð�(��)

// --- ���� ���� ���� ---
std::atomic<bool> bExitWorker{ false };

std::atomic<uint64_t> enqCount{ 0 };     // �� enqueue Ƚ��
std::atomic<uint64_t> deqCount{ 0 };     // �� dequeue Ƚ��
std::atomic<uint64_t> queueSize{ 0 };    // ���� ť ũ��
std::atomic<uint64_t> maxQueueSize{ 0 };// ��ũ ť ũ��

// TBB �� Lock-free ť �ν��Ͻ�
tbb::concurrent_queue<int>    g_TBBQueue;
lockfree_queue<int>           g_LFQueue;

// --- ���� �ڵ鷯 ---
// ���� �� ġ���� ���� �߻� �� ȣ��˴ϴ�.
static void handler() {
    std::cerr << "[Error] handler ȣ��: �۾� �� ����ġ ���� ���� �߻�\n";
    exit(1);
}

// --- ��ũ ť ũ�� ���� �Լ� ---
void updateMaxSize(uint64_t current) {
    uint64_t prev = maxQueueSize.load(std::memory_order_relaxed);
    while (current > prev &&
        !maxQueueSize.compare_exchange_weak(prev, current, std::memory_order_relaxed)) {
        // prev�� ������Ʈ �� ������ �ݺ�
    }
}

// --- ��Ŀ ������ �Լ� ---
unsigned int __stdcall Worker(void* /*arg*/) {
    if (test == 1) {
        // lockfree_queue ��ġ��ũ
        while (!bExitWorker.load(std::memory_order_relaxed)) {
            // Enqueue ��ġ
            for (int i = 0; i < workBatch; ++i) {
                g_LFQueue.enqueue(i);
                uint64_t sz = ++queueSize;
                updateMaxSize(sz);
                ++enqCount;
            }

            // Dequeue ��ġ
            int v;
            for (int i = 0; i < workBatch; ++i) {
                if (!g_LFQueue.dequeue(v)) handler();
                --queueSize;
                ++deqCount;
            }
        }
    }
    else {
        // tbb::concurrent_queue ��ġ��ũ
        while (!bExitWorker.load(std::memory_order_relaxed)) {
            // Enqueue ��ġ
            for (int i = 0; i < workBatch; ++i) {
                g_TBBQueue.push(i);
                uint64_t sz = ++queueSize;
                updateMaxSize(sz);
                ++enqCount;
            }

            // Dequeue ��ġ
            int v;
            for (int i = 0; i < workBatch; ++i) {
                if (!g_TBBQueue.try_pop(v)) handler();
                --queueSize;
                ++deqCount;
            }
        }
    }
    return 0;
}

// --- ����� ������ �Լ� ---
unsigned int __stdcall Monitor(void* /*arg*/) {
    std::vector<uint64_t> enqPerSec;
    std::vector<uint64_t> deqPerSec;
    enqPerSec.reserve(benchmarkDuration);
    deqPerSec.reserve(benchmarkDuration);

    uint64_t lastEnq = enqCount.load();
    uint64_t lastDeq = deqCount.load();

    // ��ġ��ũ �Ⱓ ���� 1�� ������ ó���� ����
    for (int i = 0; i < benchmarkDuration; ++i) {
        Sleep(1000);
        uint64_t nowEnq = enqCount.load();
        uint64_t nowDeq = deqCount.load();

        enqPerSec.push_back(nowEnq - lastEnq);
        deqPerSec.push_back(nowDeq - lastDeq);

        lastEnq = nowEnq;
        lastDeq = nowDeq;
    }

    // ��Ŀ ������ ���� ��ȣ
    bExitWorker.store(true);

    // ��� ��� �Լ�
    auto computeStats = [&](const std::vector<uint64_t>& data) {
        double avg = std::accumulate(data.begin(), data.end(), 0.0) / data.size();
        auto [minIt, maxIt] = std::minmax_element(data.begin(), data.end());
        return std::make_tuple(avg, *minIt, *maxIt);
        };

    auto [enqAvg, enqMin, enqMax] = computeStats(enqPerSec);
    auto [deqAvg, deqMin, deqMax] = computeStats(deqPerSec);

    // ��� ���
    std::cout << "\n=== ��ġ��ũ ��� ("
        << (test == 1 ? "lockfree_queue" : "tbb::concurrent_queue")
        << ", ������=" << numThreads << ") ===\n";

    std::cout << "Enqueue/sec : ���=" << std::fixed << std::setprecision(2) << enqAvg
        << ", �ּ�=" << enqMin << ", �ִ�=" << enqMax << "\n";

    std::cout << "Dequeue/sec : ���=" << deqAvg
        << ", �ּ�=" << deqMin << ", �ִ�=" << deqMax << "\n";

    std::cout << "�ִ� ť ũ�� : " << maxQueueSize.load() << "\n";
    std::cout << "�� enqueue  : " << enqCount.load()
        << ", �� dequeue : " << deqCount.load() << "\n";

    return 0;
}

// --- ���� �Լ� ---
int main() {
    std::vector<int> tests = { 1, 2 };
    std::vector<int> threadCounts = { 2, 4, 8 };

    for (int t : tests) {
        for (int nt : threadCounts) {
            test = t;
            numThreads = nt;

            // ���� ���� �ʱ�ȭ
            bExitWorker.store(false);
            enqCount.store(0);
            deqCount.store(0);
            queueSize.store(0);
            maxQueueSize.store(0);

            // ��ġ��ũ ���� �޽���
            std::cout << " ��ġ��ũ ���� : " << (test == 1 ? "lockfree_queue" : "tbb::concurrent_queue")
                << ", ������=" << numThreads << ", ��ġ=" << workBatch
                << ", �ð�=" << benchmarkDuration << "��\n";

                // ����� ������ ����
                HANDLE hMon = (HANDLE)_beginthreadex(nullptr, 0, Monitor, nullptr, 0, nullptr);

            // ��Ŀ ������ ����
            std::vector<HANDLE> workers;
            workers.reserve(numThreads);
            for (int i = 0; i < numThreads; ++i) {
                workers.push_back((HANDLE)_beginthreadex(nullptr, 0, Worker, nullptr, 0, nullptr));
            }

            // ��Ŀ ���� ���
            WaitForMultipleObjects(static_cast<DWORD>(workers.size()), workers.data(), TRUE, INFINITE);

            // ����� ���� ���
            WaitForSingleObject(hMon, INFINITE);

            // TBB ť ���� (���� ������ ����)
            if (test == 2) {
                int tmp;
                while (g_TBBQueue.try_pop(tmp));
            }

            std::cout << "\n\n";
        }
    }

    ExitProcess(0);
}
