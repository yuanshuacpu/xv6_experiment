// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW 0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe* utf) {
    void* addr = (void*) utf->utf_fault_va;
    uint32_t err = utf->utf_err;
    int r;
    extern volatile pte_t uvpt[];

    // Check that the faulting access was (1) a write, and (2) to a
    // copy-on-write page.  If not, panic.
    // Hint:
    //   Use the read-only page table mappings at uvpt
    //   (see <inc/memlayout.h>).

    // LAB 4: Your code here.

    // cprintf("pg fault va:%08x,error number:%d\n", utf->utf_fault_va, utf->utf_err);
    if (!(err & FEC_WR)) {
        panic("pg fault va:%08x,error number:%d\n", utf->utf_fault_va, utf->utf_err);
    }

    pte_t* ptab = (pte_t*) (uvpt + ((PDX(addr) << PTXSHIFT) / 4));

    pte_t ptab_pte = ptab[PTX(addr)];

    if (!(ptab_pte & PTE_COW)) {
        panic("pg fault permission is not allowed\n");
    }

    // Allocate a new page, map it at a temporary location (PFTEMP),
    // copy the data from the old page to the new page, then move the new
    // page to the old page's address.
    // Hint:
    //   You should make three system calls.

    // LAB 4: Your code here.
    envid_t id = sys_getenvid();

    r = sys_page_alloc(id, PFTEMP, PTE_U | PTE_P | PTE_W);
    if (r != 0) {
        panic("sys_page_alloc error:%d", r);
    }

    memcpy(PFTEMP, ROUNDDOWN(addr, PGSIZE), PGSIZE);
    r = sys_page_map(id, (void*) PFTEMP, id, ROUNDDOWN(addr, PGSIZE), PTE_U | PTE_P | PTE_W);
    if (r != 0) {
        panic("sys_page_map error:%d", r);
    }

    r = sys_page_unmap(id, PFTEMP);
    if (r != 0) {
        panic("sys_page_unmap error:%d", r);
    }
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//

static int
duppage(envid_t envid, unsigned pn) {
    int r;

    // LAB 4: Your code here.
    envid_t parent_envid = sys_getenvid();
    int ret = sys_page_map(parent_envid, (void*) pn, envid, (void*) pn, PTE_COW | PTE_U | PTE_P);

    if (ret != 0) {
        cprintf("duppage sys_page_map error:%d\n", ret);
        return ret;
    }

    ret = sys_page_map(parent_envid, (void*) pn, parent_envid, (void*) pn, PTE_COW | PTE_U | PTE_P);
    if (ret != 0) {
        cprintf("duppage self sys_page_map error:%d\n", ret);
        return ret;
    }
    return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
pde_t pdir_pte_debug = 0;
pte_t* ptable_debug;
size_t i_debug = 0;
size_t j_debug = 0;
pde_t ptable_pte_debug = 0;
uintptr_t ptable_pte_addr_debug = 0;
void* final_addr;
size_t i_final_debug = 0;
envid_t
fork(void) {
    // LAB 4: Your code here.
    extern volatile pde_t uvpd[];
    extern volatile pte_t uvpt[];
    extern void _pgfault_upcall(void);
    int ret = 0;

    envid_t parent_envid = sys_getenvid();

    set_pgfault_handler(pgfault);

    envid_t chile_envid = sys_exofork();

    if (chile_envid == 0) {
        chile_envid = sys_getenvid();
        thisenv = &envs[ENVX(chile_envid)];
        return 0;
    }

    ret = sys_page_alloc(chile_envid, (void*) (UXSTACKTOP - PGSIZE), PTE_W | PTE_U | PTE_P);
    if (ret != 0) {
        cprintf("child sys_page_alloc error\n");
        return ret;
    }
    sys_env_set_pgfault_upcall(chile_envid, (void*) _pgfault_upcall);

    size_t utop_end_number = PDX(USTACKTOP);
    size_t end_number = PGSIZE / 4;
    for (size_t i = 0; i <= utop_end_number; i++) {
        i_debug = i;
        pdir_pte_debug = uvpd[i];

        pte_t pdir_pte = uvpd[i];
        if (pdir_pte == 0) {
            continue;
        }
        pte_t* ptable = (pte_t*) (uvpt + (i << 10));
        ptable_debug = ptable;
        if (i == utop_end_number) {
            end_number = PTX(USTACKTOP);
            i_final_debug = end_number;
        }
        for (size_t j = 0; j < end_number; j++) {
            j_debug = j;
            pte_t ptable_pte = ptable[j];
            ptable_pte_addr_debug = (uintptr_t) &ptable[j];
            ptable_pte_debug = ptable_pte;
            final_addr = PGADDR(i, j, 0);
            if (ptable_pte == 0)
                continue;
            if (ptable_pte & PTE_U) {
                if ((ptable_pte & PTE_W) || (ptable_pte & PTE_COW)) {
                    ret = duppage(chile_envid, (unsigned) PGADDR(i, j, 0));
                    if (ret != 0) {
                        cprintf("duppage ret error %d\n", ret);
                        return ret;
                    }
                } else {
                    ret = sys_page_map(parent_envid, (void*) PGADDR(i, j, 0), chile_envid, (void*) PGADDR(i, j, 0), PTE_U | PTE_P);
                    if (ret != 0) {
                        cprintf("fork sys_page_map error:%d\n", ret);
                        return ret;
                    }
                }
            }
        }
    }
    ret = sys_env_set_status(chile_envid, ENV_RUNNABLE);
    if (ret != 0) {
        cprintf("sys_env_set_status error %d\n", ret);
        return ret;
    }
    return chile_envid;
}

// Challenge!
int sfork(void) {
    panic("sfork not implemented");
    return -E_INVAL;
}
