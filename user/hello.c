// hello, world
#include <inc/lib.h>
envid_t id = 0;
void umain(int argc, char** argv) {
    cprintf("hello, world\n");
    cprintf("i am environment %08x\n", thisenv->env_id);
}
