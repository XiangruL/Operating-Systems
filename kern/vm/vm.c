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
//file:
#include <vnode.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <uio.h>
//bitmap
#include <bitmap.h>

/*
 * Wrap ram_stealmem in a spinlock.
 */
static struct spinlock coremap_lock = SPINLOCK_INITIALIZER;
// static int fifo = 0;
static struct vnode * swap_vnode;

void
vm_bootstrap(void)
{
	/* Do nothing. */
	//1 vfs open
	if(vfs_open((char *)SWAP_FILENAME, O_RDWR, 0, &swap_vnode)){
		vm_swapenabled = false;
	}else{
		vm_swapenabled = true;
	}
	//2 bitmap create
	vm_bitmap = bitmap_create(5 * 1024 * 1024 / PAGE_SIZE);

}

int
block_write(void *buffer, off_t offset/*default size: PAGE_SIZE*/){
	struct iovec iov;
	struct uio u;

	uio_kinit(&iov, &u, buffer, PAGE_SIZE, offset, UIO_WRITE);
	int result = VOP_WRITE(swap_vnode, &u);
	if(result){
		return result;
	}
	return 0;
}

int
block_read(void * buffer, off_t offset){
	struct iovec iov;
	struct uio u;

	uio_kinit(&iov, &u, buffer, PAGE_SIZE, offset, UIO_READ);
	int result = VOP_READ(swap_vnode, &u);
	if(result){
		return result;
	}
	return 0;
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
				coremap[k].cm_inPTE = false;
                // coremap[k].cm_size = PAGE_SIZE;
                if(k == i - npages + 1){
                    coremap[k].cm_len = npages;
                }
            }
            spinlock_release(&coremap_lock);
            return PADDR_TO_KVADDR(pa);
        }
    }
	//fifo = (cm-entry-fifo + 1) % FIFO
    spinlock_release(&coremap_lock);
	return 0;// no
}

vaddr_t
user_alloc_onepage()
{
	paddr_t pa = 0;
    spinlock_acquire(&coremap_lock);

    for(unsigned i = cm_addr / PAGE_SIZE ; i < ram_getsize()/PAGE_SIZE; i++){
        if(coremap[i].cm_status == Free){
			pa = i * PAGE_SIZE;
            coremap[i].cm_status = Dirty;
			coremap[i].cm_inPTE = true;
            coremap[i].cm_len = 1;
			coremap[i].cm_pid = curproc->p_PID;
			spinlock_release(&coremap_lock);
	        return PADDR_TO_KVADDR(pa);
        }
    }
	//try swap out:
	//1. find a coremap index

	//2. check its status and block_write(may not need)

	//3. modify its status

	//4. tlbshootdown

	//5. return physical addrspace

    spinlock_release(&coremap_lock);
	return 0;// no
}

void
free_kpages(vaddr_t addr)
{
	// (void)addr;
    paddr_t paddr1 = addr - MIPS_KSEG0;
    if(paddr1 > ram_getsize()){
        return;
    }
    int index = paddr1 / PAGE_SIZE;

    // synchronization
    spinlock_acquire(&coremap_lock);
    if(coremap[index].cm_len >= 1){
        for(unsigned i = 0; i < coremap[index].cm_len; i++){
            coremap[index + i].cm_status = Free;
			coremap[index + i].cm_inPTE = false;
			coremap[index + i].cm_pid = -1;
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
	if(faultaddress >= as->heap_vbase + as->heap_vbound * PAGE_SIZE && faultaddress < stackbase){
		return EFAULT;
	}

	// stack or heap:
	// do nothing in this step and skip to create new pte.
	// non-stack:

	struct regionInfoNode * tmp = as->regionInfo;
	//faultaddress should be only in non-stack non-heap regions.
	if(faultaddress < as->heap_vbase){
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
			// kprintf("vm.c invalid faultaddress");
			return EFAULT;// invalid faultaddress
		}
	}

	struct pageTableNode * ptTmp = as->pageTable;
	bool found = 0;
	while(ptTmp != NULL){
		if(ptTmp->pt_vas == faultaddress){
			found = 1;
			break;
		}
		ptTmp = ptTmp->next;
	}

	if(found == 1){

		//1. check ptTmp status

		//1.1 if in disk, user_alloc_onepage, then swap in

		//1.2 if in memory
		paddr1 = ptTmp->pt_pas;
	}else{
		//create
		struct pageTableNode * newpt;
		newpt = (struct pageTableNode *)kmalloc(sizeof(struct pageTableNode));
		if(newpt == NULL){
			return ENOMEM;
		}
		newpt->pt_vas = faultaddress;
		newpt->pt_isDirty = true;
		newpt->pt_inDisk = false;
		newpt->pt_diskOffset = 0;
		vaddr_t vaddr_tmp = user_alloc_onepage();//alloc_kpages(1);
		// kprintf("%x\n", vaddr_tmp);
		if(vaddr_tmp == 0){
			kfree(newpt);
			return ENOMEM;
		}
		newpt->pt_pas = vaddr_tmp - MIPS_KSEG0;
		paddr1 = newpt->pt_pas;
		bzero((void *)PADDR_TO_KVADDR(paddr1), 1 * PAGE_SIZE);
		// newpt->pt_pas |= tmp->as_permission;

		newpt->next = as->pageTable;
		as->pageTable = newpt;
		//if pte is in heap
		if(newpt->pt_vas >= as->heap_vbase && newpt->pt_vas < as->heap_vbase + as->heap_vbound * PAGE_SIZE){
			// as->heap_page_used++;
		}
	}
	//update TLB
	/* make sure it's page-aligned */
	KASSERT((paddr1 & PAGE_FRAME) == paddr1);
	uint32_t ehi, elo;
	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();
	ehi = faultaddress;

	int i;
	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if((ehi & PAGE_FRAME) == faultaddress || !(elo & TLBLO_VALID)){
			ehi = faultaddress;
			elo = paddr1 | TLBLO_DIRTY | TLBLO_VALID;
			DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr1);
			tlb_write(ehi, elo, i);
			splx(spl);
			return 0;
		}else{
			continue;
		}

	}
	ehi = faultaddress;
	elo = paddr1 | TLBLO_DIRTY | TLBLO_VALID;
	tlb_random(ehi, elo);
	splx(spl);
	return 0;
}
