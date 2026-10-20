/* No allocation, syscalls or global pointers. State is private RAM padding.
 * Only panels visited by the explicit owner in the previous frame are deferred.
 * Misses, ownership gaps, other threads and capacity overflow use stock behavior.
 */
typedef unsigned long long U;
typedef unsigned int W;
typedef struct { U controller, entity, seen; W pending, desired; } Entry;
typedef struct {
    U set_enabled, get_node, thread, epoch, inflight, deferred, flushes, misses;
    Entry entries[64];
} State;
static U thread_id(void) { U t; __asm__("mrs %0,tpidr_el0":"=r"(t)); return t; }
static void set(State* s, U node, W value) {
    ((void(*)(U,W))s->set_enabled)(node,value);
}
static Entry* find(State* s,U node) {
    U entity=*(W*)node;
    U controller=*(U*)(node+8);
    for(W i=0;i<64;i++)
        if(s->entries[i].controller==controller && s->entries[i].entity==entity) return &s->entries[i];
    return (Entry*)0;
}
void panel_frame(State* s) {
    U t=thread_id();
    if(!s->thread) s->thread=t;
    if(s->thread!=t) return;
    s->epoch++;
}
void panel_mirror(U node,W desired,State* s) {
    __atomic_add_fetch(&s->inflight,1,__ATOMIC_SEQ_CST);
    if(s->thread!=thread_id()) {
        set(s,node,desired);
        __atomic_sub_fetch(&s->inflight,1,__ATOMIC_SEQ_CST);
        return;
    }
    Entry* e=find(s,node);
    if(e && e->seen+1==s->epoch) {
        e->pending=0; e->desired=desired;
        U controller=*(U*)(node+8);
        if(desired && !(*(W*)(controller+32)&1)) {
            e->pending=1;
            s->deferred++;
            __atomic_sub_fetch(&s->inflight,1,__ATOMIC_SEQ_CST);
            return;
        }
    } else s->misses++;
    set(s,node,desired);
    __atomic_sub_fetch(&s->inflight,1,__ATOMIC_SEQ_CST);
}
void panel_owner(U handle,W visible,State* s) {
    if(s->thread!=thread_id()) return;
    __atomic_add_fetch(&s->inflight,1,__ATOMIC_SEQ_CST);
    U node=((U(*)(U))s->get_node)(handle);
    Entry* e=find(s,node);
    if(e) {
        if(e->pending && e->seen+1==s->epoch) {
            /* Hidden owner already calls SetEnabled(false) below our hook.
             * Only a newly visible panel needs its deferred enable flushed. */
            if(visible && e->desired) set(s,node,1);
            s->flushes++;
        }
    } else {
        for(W i=0;i<64;i++) {
            if(!s->entries[i].controller || s->entries[i].seen+1<s->epoch) {
                e=&s->entries[i]; e->controller=*(U*)(node+8); e->entity=*(W*)node; break;
            }
        }
    }
    if(e) { e->seen=s->epoch; e->pending=0; }
    __atomic_sub_fetch(&s->inflight,1,__ATOMIC_SEQ_CST);
}
