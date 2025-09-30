// fgq_mt_demo.cpp
#include <iostream>
#include <thread>
#include <vector>
#include <random>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include "FineGrainedQueue.h"
using namespace std;

struct DemoCfg {
    int threads;
    int per_thread;
    int initial;
    int maxPos;
};

// Профили нагрузки:
// QUICK   — быстрый прогон (<1 c в Debug)
// BALANCED— ~2–4 c в Debug на типичном ПК
// STRESS  — тяжёлый стресс (может идти долго в Debug)
static const DemoCfg QUICK{ 4, 1000, 200,  1000 };
static const DemoCfg BALANCED{ 6, 3000, 300,  5000 };
static const DemoCfg STRESS{ 8, 5000, 1000, 200000 };

static int run_demo(const DemoCfg& cfg) {
    cout << "ПРЕДУПРЕЖДЕНИЕ: Сейчас запустится проверка многопоточности.\n"
        "В Debug-сборке она может занять примерно 3–6 секунд. Пожалуйста, не закрывайте окно.\n\n";

    cout << "Тест многопоточности: FineGrainedQueue ("
        << cfg.threads << " поток(ов), " << cfg.per_thread << " вставок/поток, initial="
        << cfg.initial << ", maxPos=" << cfg.maxPos << ")\n";

    FineGrainedQueue q;
    for (int i = 0; i < cfg.initial; ++i) q.push_back(i);

    mutex start_m;
    condition_variable cv;
    bool start_flag = false;

    vector<thread> ws;
    auto t0 = chrono::steady_clock::now();

    for (int t = 0; t < cfg.threads; ++t) {
        ws.emplace_back([&, t] {
            // барьер старта — чтобы потоки реально пошли параллельно
            { unique_lock<mutex> lk(start_m); cv.wait(lk, [&] { return start_flag; }); }

            mt19937 rng((unsigned)random_device {}() ^ (t * 0x9E3779B1));
            uniform_int_distribution<int> posdist(1, cfg.maxPos);
            const int base = 1'000'000 * (t + 1); // уникальные значения на поток

            for (int i = 0; i < cfg.per_thread; ++i) {
                q.insertIntoMiddle(base + i, posdist(rng));
                if ((i & 0x03FF) == 0) this_thread::yield();
            }
            cout << "[поток " << t << "] завершил вставки\n";
            });
    }

    { lock_guard<mutex> lk(start_m); start_flag = true; }
    cv.notify_all();

    for (auto& th : ws) th.join();

    auto t1 = chrono::steady_clock::now();
    auto ms = chrono::duration_cast<chrono::milliseconds>(t1 - t0).count();

    auto v = q.toVector(); // после join — безопасно
    const size_t expected = size_t(cfg.initial) + size_t(cfg.threads) * size_t(cfg.per_thread);

    cout << "Итоговый размер = " << v.size() << " (ожидалось " << expected << ")\n";
    cout << "Время: " << ms << " мс\n";
    if (v.size() == expected) {
        cout << "[OK] Тест успешен: многопоточность есть, вставки из нескольких потоков отработали корректно.\n";
    }
    else {
        cout << "[ОШИБКА] Несовпадение размера — проверь дисциплину блокировок и логику вставки.\n";
    }
    return 0;
}

int fine_grained_queue_mt_demo() {
    // Для защиты и быстрой проверки используй BALANCED:
    return run_demo(BALANCED);

    // Можно быстро переключать профиль при отладке:
    // return run_demo(QUICK);
    // return run_demo(STRESS);
}
