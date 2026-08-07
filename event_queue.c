struct event {
    xkb_keysym_t sym;
    enum wl_keyboard_key_state state;
    struct timespec time;
};

#define EVENT_QUEUE_SIZE 256

// INIT VALUES::
// front = 0
// rear = 0
struct event_queue {
    struct event items[EVENT_QUEUE_SIZE];
    int32_t front;
    int32_t rear;
    uint32_t size;
};

bool event_queue_is_empty(const struct event_queue* q) {
    return q->size == 0;
}

void event_enqueue(struct event_queue* q, const struct event* e) {
    if(q->size == EVENT_QUEUE_SIZE) 
        return; // ignore inputs if too many inputs
    q->items[q->rear] = *e;
    q->rear = (q->rear + 1) % EVENT_QUEUE_SIZE;
    q->size ++;
}

bool event_dequeue(struct event_queue* q, struct event* e) {
    if(event_queue_is_empty(q))
        return false;

    *e =  q->items[q->front];
    q->front = (q->front + 1) % EVENT_QUEUE_SIZE;
    q->size --;

    return true;
}
