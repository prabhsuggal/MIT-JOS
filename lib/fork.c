// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	pte_t pte = uvpt[PGNUM(addr)];
	if(!(err & FEC_WR)){
		panic("pgfault: given access not a write");
	}
	else if(!(pte&PTE_COW)){
		panic("pgfault: Not a COW page");
	}
	// LAB 4: Your code here.

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
	int perm = PTE_P | PTE_W | PTE_U;
	r = sys_page_alloc(0, (void*)PFTEMP, perm);
	if(r < 0)panic("sys_page_alloc : %e", r);

	memmove(PFTEMP, ROUNDDOWN(addr, PGSIZE), PGSIZE);

	r = sys_page_map(0, (void*)PFTEMP, 0, (void*)ROUNDDOWN(addr, PGSIZE), perm);
	if(r < 0)panic("sys_page_map : %e", r);

	r = sys_page_unmap(0, PFTEMP);
	if(r < 0)panic("sys_page_unmap : %e", r);

	//panic("pgfault not implemented");
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
duppage(envid_t envid, unsigned pn)
{
	int r;

	pte_t pte = uvpt[pn];

	if(!(pte & PTE_W) && !(pte & PTE_COW)){
		r = sys_page_map(thisenv->env_id,
						  (void*)(pn*PGSIZE),
						  envid,
						  (void*)(pn*PGSIZE), pte & PTE_SYSCALL);
		if(r < 0)panic("duppage: %e", r);
		return 0;
	}

	pte &= ~PTE_W;
	pte |= PTE_COW;

	r = sys_page_map(thisenv->env_id, (void*)(pn*PGSIZE),
					envid, (void*)(pn*PGSIZE), pte & PTE_SYSCALL);
	if(r < 0)panic("duppage: %e", r);

	r = sys_page_map(thisenv->env_id, (void*)(pn*PGSIZE),
					thisenv->env_id, (void*)(pn*PGSIZE), pte & PTE_SYSCALL);
	if(r < 0)panic("duppage: %e", r);

	// LAB 4: Your code here.
	// panic("duppage not implemented");
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
envid_t
fork(void)
{
	int rc;
	set_pgfault_handler(pgfault);

	envid_t childId = sys_exofork();
	if(childId < 0){
		panic("sys_exofork: %e", childId);
	}
	else if(childId==0){
		thisenv = &envs[ENVX(sys_getenvid())];
		return childId;
	}

	bool is_below_ulim = true;

	for(int i=0; is_below_ulim && i < NPDENTRIES; i++){
		if(!(uvpd[i] && PTE_P))
			continue;

		for(int j=0; is_below_ulim && j < NPTENTRIES; j++){
			unsigned pn = i*NPTENTRIES + j;
			if(pn == ((UXSTACKTOP-PGSIZE) >> PGSHIFT))continue;
			else if(pn >= UTOP >> PGSHIFT){
				is_below_ulim = true;
				continue;
			}
			else if(uvpt[pn] & PTE_P){
				duppage(childId, pn);
			}
		}
	}

	// install upcall
	extern void _pgfault_upcall();
	assert(sys_env_set_pgfault_upcall(childId, _pgfault_upcall) == 0);
	// allocate the user exception stack (not COW)
	assert(sys_page_alloc(childId, (void *)(UXSTACKTOP - PGSIZE), PTE_P | PTE_W | PTE_U) == 0);
	// let the child start
	assert(sys_env_set_status(childId, ENV_RUNNABLE) == 0);

	return childId;

	// LAB 4: Your code here.
	//panic("fork not implemented");
}

// Challenge!
int
sfork(void)
{
	int rc;
	set_pgfault_handler(pgfault);

	envid_t childId = sys_exofork();
	if(childId < 0){
		panic("sys_exofork: %e", childId);
	}
	else if(childId==0){
		thisenv = &envs[ENVX(sys_getenvid())];
		return childId;
	}

	bool is_below_ulim = true;

	for(int i=0; is_below_ulim && i < NPDENTRIES; i++){
		if(!(uvpd[i] && PTE_P))
			continue;
		for(int j=0; is_below_ulim && j < NPTENTRIES; j++){
			unsigned pn = i*NPTENTRIES + j;
			if(pn == ((UXSTACKTOP-PGSIZE) >> PGSHIFT))continue;
			else if(pn >= UTOP >> PGSHIFT){
				is_below_ulim = true;
				continue;
			}
			else if(uvpt[pn] & PTE_P){
				duppage(childId, pn);
			}
		}
	}

	// install upcall
	extern void _pgfault_upcall();
	assert(sys_env_set_pgfault_upcall(childId, _pgfault_upcall) == 0);
	// allocate the user exception stack (not COW)
	assert(sys_page_alloc(childId, (void *)(UXSTACKTOP - PGSIZE), PTE_P | PTE_W | PTE_U) == 0);
	// let the child start
	assert(sys_env_set_status(childId, ENV_RUNNABLE) == 0);

	return childId;
	//panic("sfork not implemented");
	//return -E_INVAL;
}
