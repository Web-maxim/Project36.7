// FineGrainedQueue.h
#pragma once
#include <mutex>
#include <vector>
using namespace std;

struct Node {
    int value;
    Node* next;
    mutex* node_mutex;

    Node(int v) : value(v), next(nullptr), node_mutex(new mutex) {}
    ~Node() { delete node_mutex; }
};

class FineGrainedQueue {
public:
    FineGrainedQueue();
    ~FineGrainedQueue();

    // Вспомогательное: заполнить очередь (хвост)
    void push_back(int value);

    // Вставка в середину/конец: pos ≥ 1 (вставку в начало не рассматриваем).
    // Если pos больше длины — вставляем в хвост.
    void insertIntoMiddle(int value, int pos);

    // Необязательное: сдампить вектор значений (удобно для теста)
    vector<int> toVector() const;

private:
    Node* head;
    mutex* queue_mutex; // защищает доступ к head (получение стартовой пары узлов)
};
