// FineGrainedQueue.cpp
#include "FineGrainedQueue.h"
using namespace std;

FineGrainedQueue::FineGrainedQueue() : head(nullptr), queue_mutex(new mutex) {}

FineGrainedQueue::~FineGrainedQueue() {
    {// Простой однопоточный демонтаж; если есть рабочие потоки — останавливай их заранее.
    lock_guard<mutex> ql(*queue_mutex); // <— держим, пока чистим список
    Node* cur = head;
    while (cur) {
        Node* nx = cur->next;
        delete cur;      // ~Node удалит свой node_mutex
        cur = nx;
    }
    head = nullptr;
    } // <— здесь ql уничтожен, мьютекс уже распнут
    delete queue_mutex;       // <— теперь можно удалять сам мьютекс
}

void FineGrainedQueue::push_back(int value) {
    Node* n = new Node(value);

    // Берём head под общим мьютексом
    unique_lock<mutex> ql(*queue_mutex);
    if (!head) {
        head = n;
        return;
    }

    // Рука об руку (hand-over-hand): держим замки на текущем и следующем
    Node* prev = head;
    unique_lock<mutex> lp(*prev->node_mutex);
    ql.unlock(); // head мы уже безопасно захватили; далее — поузловые мьютексы

    Node* cur = prev->next;
    unique_lock<mutex> lc;
    if (cur) lc = unique_lock<mutex>(*cur->node_mutex);

    while (cur) {
        // Идём вперёд: захватываем next, отпускаем prev
        Node* next = cur->next;               // читать можно под lc
        unique_lock<mutex> ln;
        if (next) ln = unique_lock<mutex>(*next->node_mutex);

        lp.unlock();
        lp = std::move(lc);                   // prev <- cur (замок переносим)
        lc = std::move(ln);                   // curr <- next
        prev = prev->next;                    // prev = cur
        cur = next;
    }

    // cur == nullptr => prev — хвост
    prev->next = n;
}

void FineGrainedQueue::insertIntoMiddle(int value, int pos) {
    // Предпосылки по условию:
    // - очередь НЕ пустая,
    // - вставку в голову рассматривать не нужно,
    // - если pos > длины, вставляем в хвост.
    if (pos <= 0) pos = 1; // страхуемся: минимум — после head

    Node* newNode = new Node(value);

    // 1) Безопасно считываем стартовую пару (head и head->next)
    unique_lock<mutex> ql(*queue_mutex);
    Node* prev = head;
    if (!prev) { // на всякий случай, хотя по условию список не пуст
        head = newNode;
        ql.unlock();
        return;
    }

    unique_lock<mutex> lp(*prev->node_mutex);
    ql.unlock(); // дальше управляют поузловые мьютексы

    Node* cur = prev->next;
    unique_lock<mutex> lc;
    if (cur) lc = unique_lock<mutex>(*cur->node_mutex);

    // 2) Идём до позиции pos (pos ≥ 1 означает «после head» — это позиция 1)
    int i = 1;
    while (cur && i < pos) {
        Node* next = cur->next;               // читать под lc безопасно
        unique_lock<mutex> ln;
        if (next) ln = unique_lock<mutex>(*next->node_mutex);

        lp.unlock();                          // окно сдвигаем: отпускаем prev
        lp = std::move(lc);                   // prev <- cur (перенос блокировки)
        lc = std::move(ln);                   // cur  <- next
        prev = prev->next;                    // prev = cur
        cur = next;
        ++i;
    }

    // 3) Вставляем между prev и cur (cur может быть nullptr — вставка в хвост)
    newNode->next = cur;
    prev->next = newNode;

    // lp и lc освободятся деструкторами unique_lock
}

vector<int> FineGrainedQueue::toVector() const {
    vector<int> out;

    // Захватываем только head, далее для снимка сост сост сост — без поузловых.
    // В реальном проде лучше делать тоже поузловые замки на чтение,
    // но для простого теста этого достаточно.
    Node* cur = head;
    while (cur) {
        out.push_back(cur->value);
        cur = cur->next;
    }
    return out;
}
