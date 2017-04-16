#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <elf.h>

#define VM_STACKPAGES    1024

/*
 * Wrap ram_stealmem in a spinlock.
 */
static struct spinlock coremap_lock = SPINLOCK_INITIALIZER;
void
vm_bootstrap(void)
{
	/* Do nothing. */
}


vaddr_t
alloc_kpages(unsigned npages)
{
	paddr_t pa = 0;
    unsigned tmp = 0;
    spinlock_acquire(&coremap_lock);
    for(unsigned i = cm_addr / PAGE_SIZE ; i < ram_getsize()/PAGE_SIZE; i++){
        if(coremap[i].cm_status == Free){
            tmp++;
        }else{
            tmp = 0;
        }
        if(tmp == npages){
            pa = (i - npages + 1) * PAGE_SIZE;
            for(unsigned k = i - npages + 1; k <= i;k++){
                coremap[k].cm_status = Dirty;
                // coremap[k].cm_size = PAGE_SIZE;
                if(k == i - npages + 1){
                    coremap[k].cm_len = npages;
                }
            }
            spinlock_release(&coremap_lock);
            return PADDR_TO_KVADDR(pa);
        }
    }
    spinlock_release(&coremap_lock);
	return 0;// no
}

void
free_kpages(vaddr_t addr)
{
	// (void)addr;
    paddr_t paddr1 = addr - MIPS_KSEG0;
    if(paddr1 > ram_getsize()){
        kprintf("vm.c free_kpages - invalid addr");
    }
    int index = paddr1 / PAGE_SIZE;

    // synchronization
    spinlock_acquire(&coremap_lock);
    if(coremap[index].cm_len >= 1){
        for(unsigned i = 0; i < coremap[index].cm_len; i++){
            coremap[index + i].cm_status = Free;
            // coremap[index + i].cm_size = 0;
        }
        coremap[index].cm_len = 0;
    }
    spinlock_release(&coremap_lock);

}

unsigned
int
coremap_used_bytes() {

	/* dumbvm doesn't track page allocations. Return 0 so that khu works. */
    unsigned int res = 0;
    spinlock_acquire(&coremap_lock);
    for(unsigned i = 0; i < ram_getsize()/PAGE_SIZE; i++){
        if(coremap[i].cm_status != Free){
            res++;
        }
    }
    spinlock_release(&coremap_lock);
    return res*PAGE_SIZE;
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	vaddr_t vbase, vtop, stackbase, stacktop;
	paddr_t paddr1 = 0x0;
	struct addrspace *as;
	int spl;

	faultaddress &= PAGE_FRAME;

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		/* We always create pages read-write, so we can't get this */
		panic("vm: got VM_FAULT_READONLY\n");
	    case VM_FAULT_READ://0x0
	    case VM_FAULT_WRITE://0x1
		break;
	    default:
		return EINVAL;
	}

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = proc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	/* Assert that the address space has been set up properly. */

	// faultaddress should belong to one regions

	stackbase = USERSTACK - VM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;
	if(faultaddress >= stacktop){
		return EFAULT;
	}
	if(faultaddress >= as->heap_vbound && faultaddress < stackbase){
		return EFAULT;
	}
	// stack or heap:
	// do nothing in this step and skip to create new pte.
	// non-stack:

	struct regionInfoNode * tmp = as->regionInfo;
	//faultaddress should be only in non-stack non-heap regions.
	if(faultaddress < as->heap_vbound){
		while(tmp != NULL){
			//KASSERT
			KASSERT(tmp->as_vbase != 0);
			KASSERT((tmp->as_vbase & PAGE_FRAME) == tmp->as_vbase);
			//base & bounds
			vbase = tmp->as_vbase;
			vtop = vbase + tmp->as_npages * PAGE_SIZE;
			if(faultaddress >= vbase && faultaddress < vtop){
				break;
			}else{
				tmp = tmp->next;
			}
		}
		if(tmp == NULL){
			return EFAULT;// invalid faultaddress
		}
	}

	struct pageTableNode * ptTmp = as->pageTable;
	bool found = 0;
	while(ptTmp != NULL){
		if(ptTmp->pt_vas == (faultaddress & PAGE_FRAME)){
			found = 1;
			break;
		}
		ptTmp = ptTmp->next;
	}

	if(found == 1){
		if(((ptTmp->pt_pas & PF_R) == PF_R && faulttype == VM_FAULT_READ) || ((ptTmp->pt_pas & PF_W) == PF_W && faulttype == VM_FAULT_WRITE)){
			paddr1 = ptTmp->pt_pas & PAGE_FRAME;
			//update TLB;
		}else{
			return EFAULT;
		}
	}else{
		//create
		struct pageTableNode * newpt;
		newpt = (struct pageTableNode *)kmalloc(sizeof(struct pageTableNode));
		if(newpt == NULL){
			return ENOMEM;
		}
		newpt->pt_vas = faultaddress & PAGE_FRAME;
		vaddr_t vaddr_tmp = alloc_kpages(1);
		// kprintf("%x\n", vaddr_tmp);
		if(vaddr_tmp == 0){
			return ENOMEM;
		}
		newpt->pt_pas = vaddr_tmp - MIPS_KSEG0;
		paddr1 = newpt->pt_pas;
		as_zero_region(paddr1, 1);
		newpt->pt_pas |= tmp->as_tmp_permission;
		if(as->pageTable == NULL){
			as->pageTable = newpt;
		}else{
			newpt->next = as->pageTable->next;
			as->pageTable->next = newpt;
		}
	}

	//update TLB
	/* make sure it's page-aligned */
	KASSERT((paddr1 & PAGE_FRAME) == paddr1);
	uint32_t ehi, elo;
	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();
	ehi = faultaddress;
	elo = paddr1 | TLBLO_DIRTY | TLBLO_VALID;
	tlb_random(ehi, elo);
	splx(spl);
	return 0;
}
