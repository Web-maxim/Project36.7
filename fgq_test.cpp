// fgq_test.cpp
#include <iostream>
#include "FineGrainedQueue.h"
using namespace std;
#if 0
int main_2() {
    FineGrainedQueue q;
    q.push_back(1);
    q.push_back(2);
    q.push_back(4);

    // Вставим 3 на позицию 2 (после head — это 1, после второго — 2)
    q.insertIntoMiddle(3, 2);

    for (int x : q.toVector()) cout << x << " ";
    cout << endl; // ожидаем: 1 2 3 4
    return 0;
}
#endif