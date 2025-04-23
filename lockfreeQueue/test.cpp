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

// --- 벤치마크 설정 ---
int test = 1;                // 1: lockfree_queue, 2: tbb::concurrent_queue
int numThreads = 4;          // 워커 스레드 수
const int workBatch = 10000; // 한 배치당 enqueue/dequeue 수행 횟수
const int benchmarkDuration = 3; // 벤치마크 실행 시간(초)

// --- 전역 상태 변수 ---
std::atomic<bool> bExitWorker{ false };

std::atomic<uint64_t> enqCount{ 0 };     // 총 enqueue 횟수
std::atomic<uint64_t> deqCount{ 0 };     // 총 dequeue 횟수
std::atomic<uint64_t> queueSize{ 0 };    // 현재 큐 크기
std::atomic<uint64_t> maxQueueSize{ 0 };// 피크 큐 크기

// TBB 및 Lock-free 큐 인스턴스
tbb::concurrent_queue<int>    g_TBBQueue;
lockfree_queue<int>           g_LFQueue;

// --- 에러 핸들러 ---
// 동작 중 치명적 오류 발생 시 호출됩니다.
static void handler() {
    std::cerr << "[Error] handler 호출: 작업 중 예기치 않은 오류 발생\n";
    exit(1);
}

// --- 피크 큐 크기 갱신 함수 ---
void updateMaxSize(uint64_t current) {
    uint64_t prev = maxQueueSize.load(std::memory_order_relaxed);
    while (current > prev &&
        !maxQueueSize.compare_exchange_weak(prev, current, std::memory_order_relaxed)) {
        // prev가 업데이트 될 때까지 반복
    }
}

// --- 워커 스레드 함수 ---
unsigned int __stdcall Worker(void* /*arg*/) {
    if (test == 1) {
        // lockfree_queue 벤치마크
        while (!bExitWorker.load(std::memory_order_relaxed)) {
            // Enqueue 배치
            for (int i = 0; i < workBatch; ++i) {
                g_LFQueue.enqueue(i);
                uint64_t sz = ++queueSize;
                updateMaxSize(sz);
                ++enqCount;
            }

            // Dequeue 배치
            int v;
            for (int i = 0; i < workBatch; ++i) {
                if (!g_LFQueue.dequeue(v)) handler();
                --queueSize;
                ++deqCount;
            }
        }
    }
    else {
        // tbb::concurrent_queue 벤치마크
        while (!bExitWorker.load(std::memory_order_relaxed)) {
            // Enqueue 배치
            for (int i = 0; i < workBatch; ++i) {
                g_TBBQueue.push(i);
                uint64_t sz = ++queueSize;
                updateMaxSize(sz);
                ++enqCount;
            }

            // Dequeue 배치
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

// --- 모니터 스레드 함수 ---
unsigned int __stdcall Monitor(void* /*arg*/) {
    std::vector<uint64_t> enqPerSec;
    std::vector<uint64_t> deqPerSec;
    enqPerSec.reserve(benchmarkDuration);
    deqPerSec.reserve(benchmarkDuration);

    uint64_t lastEnq = enqCount.load();
    uint64_t lastDeq = deqCount.load();

    // 벤치마크 기간 동안 1초 단위로 처리량 측정
    for (int i = 0; i < benchmarkDuration; ++i) {
        Sleep(1000);
        uint64_t nowEnq = enqCount.load();
        uint64_t nowDeq = deqCount.load();

        enqPerSec.push_back(nowEnq - lastEnq);
        deqPerSec.push_back(nowDeq - lastDeq);

        lastEnq = nowEnq;
        lastDeq = nowDeq;
    }

    // 워커 스레드 종료 신호
    bExitWorker.store(true);

    // 통계 계산 함수
    auto computeStats = [&](const std::vector<uint64_t>& data) {
        double avg = std::accumulate(data.begin(), data.end(), 0.0) / data.size();
        auto [minIt, maxIt] = std::minmax_element(data.begin(), data.end());
        return std::make_tuple(avg, *minIt, *maxIt);
        };

    auto [enqAvg, enqMin, enqMax] = computeStats(enqPerSec);
    auto [deqAvg, deqMin, deqMax] = computeStats(deqPerSec);

    // 결과 출력
    std::cout << "\n=== 벤치마크 결과 ("
        << (test == 1 ? "lockfree_queue" : "tbb::concurrent_queue")
        << ", 스레드=" << numThreads << ") ===\n";

    std::cout << "Enqueue/sec : 평균=" << std::fixed << std::setprecision(2) << enqAvg
        << ", 최소=" << enqMin << ", 최대=" << enqMax << "\n";

    std::cout << "Dequeue/sec : 평균=" << deqAvg
        << ", 최소=" << deqMin << ", 최대=" << deqMax << "\n";

    std::cout << "최대 큐 크기 : " << maxQueueSize.load() << "\n";
    std::cout << "총 enqueue  : " << enqCount.load()
        << ", 총 dequeue : " << deqCount.load() << "\n";

    return 0;
}

// --- 메인 함수 ---
int main() {
    std::vector<int> tests = { 1, 2 };
    std::vector<int> threadCounts = { 2, 4, 8 };

    for (int t : tests) {
        for (int nt : threadCounts) {
            test = t;
            numThreads = nt;

            // 상태 변수 초기화
            bExitWorker.store(false);
            enqCount.store(0);
            deqCount.store(0);
            queueSize.store(0);
            maxQueueSize.store(0);

            // 벤치마크 시작 메시지
            std::cout << " 벤치마크 시작 : " << (test == 1 ? "lockfree_queue" : "tbb::concurrent_queue")
                << ", 스레드=" << numThreads << ", 배치=" << workBatch
                << ", 시간=" << benchmarkDuration << "초\n";

                // 모니터 스레드 시작
                HANDLE hMon = (HANDLE)_beginthreadex(nullptr, 0, Monitor, nullptr, 0, nullptr);

            // 워커 스레드 시작
            std::vector<HANDLE> workers;
            workers.reserve(numThreads);
            for (int i = 0; i < numThreads; ++i) {
                workers.push_back((HANDLE)_beginthreadex(nullptr, 0, Worker, nullptr, 0, nullptr));
            }

            // 워커 종료 대기
            WaitForMultipleObjects(static_cast<DWORD>(workers.size()), workers.data(), TRUE, INFINITE);

            // 모니터 종료 대기
            WaitForSingleObject(hMon, INFINITE);

            // TBB 큐 비우기 (남은 아이템 제거)
            if (test == 2) {
                int tmp;
                while (g_TBBQueue.try_pop(tmp));
            }

            std::cout << "\n\n";
        }
    }

    ExitProcess(0);
}
