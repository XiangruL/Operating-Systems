/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <elf.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <bitmap.h>
#include <synch.h>
#include <wchan.h>
/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	/*
	 * Initialize as needed.
	 */
	as->pageTable = NULL;
	as->regionInfo = NULL;
	as->heap_vbase = 0;
	as->heap_vbound = 0;
	as->as_ptLock = lock_create("as_lock");
	if(as->as_ptLock == NULL){
		kfree(as);
		return NULL;
	}
	// as->heap_page_used = 0;
	return as;
}



void
as_destroy(struct addrspace *as)
{
	/*
	 * Clean up as needed.
	 */
	// (void)as;
	if(as == NULL){
		return;
	}
	struct pageTableNode * ptTmp = as->pageTable;
	struct pageTableNode * ptTmp2 = NULL;

	bool cm_lk_hold_before = false;
	if(!spinlock_do_i_hold(&cm_lock)){
		spinlock_acquire(&cm_lock);
	}else{
		cm_lk_hold_before = true;
	}

	while(ptTmp != NULL){
		ptTmp2 = ptTmp;
		ptTmp = ptTmp->next;

		wait_page_if_busy(ptTmp2->pt_pas / PAGE_SIZE);

		if(ptTmp2->pt_inDisk){
			KASSERT(bitmap_isset(vm_bitmap, ptTmp2->pt_bm_index) != 0);
			bitmap_unmark(vm_bitmap, ptTmp2->pt_bm_index);
		}else{
			user_free_onepage(PADDR_TO_KVADDR(ptTmp2->pt_pas));
		}
		kfree(ptTmp2);
	}

	struct regionInfoNode * riTmp = as->regionInfo;
	struct regionInfoNode * riTmp2 = NULL;
	while(riTmp != NULL){
		riTmp2 = riTmp;
		riTmp = riTmp->next;
		kfree(riTmp2);
	}

	lock_destroy(as->as_ptLock);

	kfree(as);

	if(!cm_lk_hold_before){
		spinlock_release(&cm_lock);
	}
}

void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		// kprintf("%d",i);
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
		 int readable, int writeable, int executable)
{
	size_t npages;

	/* Align the region. First, the base... */
	memsize += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	memsize = (memsize + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = memsize / PAGE_SIZE;

	int permission = readable | writeable | executable;

	struct regionInfoNode * tmp = (struct regionInfoNode*)kmalloc(sizeof(struct regionInfoNode));
	if(tmp == NULL){
        return ENOMEM;
    }
	tmp->as_vbase = vaddr;
	tmp->as_npages = npages;
	tmp->as_permission = permission;// code & data = readonly
	// tmp->as_tmp_permission = permission;

	tmp->next = as->regionInfo;
	as->regionInfo = tmp;

	if(as->heap_vbase < tmp->as_vbase + tmp->as_npages * PAGE_SIZE){
		as->heap_vbase = tmp->as_vbase + tmp->as_npages * PAGE_SIZE;
	}
	return 0;

}

int
as_prepare_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */
	// change all regions' permission to read & write
	// struct regionInfoNode * tmp = as->regionInfo;
	// while(tmp != NULL){
	// 	tmp->as_permission = PF_R | PF_W;
	// 	tmp = tmp->next;
	// }
	// as->as_stackpbase = alloc_kpages(VM_STACKPAGES);
	// if (as->as_stackpbase == 0) {
	// 	return ENOMEM;
	// }
	(void)as;
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */
	(void)as;
	// struct regionInfoNode * tmp = as->regionInfo;
 // 	while(tmp != NULL){
 // 		tmp->as_permission = tmp->as_tmp_permission;
 // 		tmp = tmp->next;
 // 	}
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */
	(void)as;
	*stackptr = USERSTACK;
	return 0;
}

static
int
PTNode_Copy(struct pageTableNode * new_ptnode, struct pageTableNode * old_ptnode){


	wait_page_if_busy(old_ptnode->pt_pas / PAGE_SIZE);

	vaddr_t vaddr_tmp;
	new_ptnode->pt_vas = old_ptnode->pt_vas;
	new_ptnode->pt_isDirty = true;
	new_ptnode->pt_inDisk = false;
	new_ptnode->pt_bm_index = 0;
	new_ptnode->next = NULL;

	vaddr_tmp = user_alloc_onepage();

	if(vaddr_tmp == 0){
		return 1;
	}
	new_ptnode->pt_pas = vaddr_tmp - MIPS_KSEG0;

	coremap[new_ptnode->pt_pas / PAGE_SIZE].cm_isbusy = true;

	bzero((void *)PADDR_TO_KVADDR(new_ptnode->pt_pas), 1 * PAGE_SIZE);
	if(old_ptnode->pt_inDisk){
		if(block_read((void *)PADDR_TO_KVADDR(new_ptnode->pt_pas), old_ptnode->pt_bm_index * PAGE_SIZE)){
			panic("block_read error in as_copy\n");
		}
	}else{
		memmove((void *)PADDR_TO_KVADDR(new_ptnode->pt_pas),
			(const void *)PADDR_TO_KVADDR(old_ptnode->pt_pas),
			1*PAGE_SIZE);
	}
	coremap[new_ptnode->pt_pas / PAGE_SIZE].cm_isbusy = false;
	wchan_wakeall(cm_wchan, &cm_lock);

	return 0;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	/*
	 * Write this.
	 */
	newas->heap_vbase = old->heap_vbase;
	newas->heap_vbound = old->heap_vbound;

	bool cm_lk_hold_before = false;
	if(!spinlock_do_i_hold(&cm_lock)){
		spinlock_acquire(&cm_lock);
	}else{
		cm_lk_hold_before = true;
	}

	//pageTable
	struct pageTableNode *oldPTtmp = old->pageTable;

	struct pageTableNode *PTtmp = NULL;
	struct pageTableNode *PTtmp2;
	while(oldPTtmp != NULL){
		//PTtmp2 init
		PTtmp2 = (struct pageTableNode*)kmalloc(sizeof(struct pageTableNode));
		if(PTtmp2 == NULL){
			as_destroy(newas);
			if(!cm_lk_hold_before){
				spinlock_release(&cm_lock);
			}
			return ENOMEM;
		}
		if(PTNode_Copy(PTtmp2, oldPTtmp)){
			kfree(PTtmp2);
			as_destroy(newas);
			if(!cm_lk_hold_before){
				spinlock_release(&cm_lock);
			}
			return ENOMEM;
		}

		PTtmp2->next = PTtmp;
		PTtmp = PTtmp2;
		oldPTtmp = oldPTtmp->next;
	}
	newas->pageTable = PTtmp;

	if(!cm_lk_hold_before){
		spinlock_release(&cm_lock);
	}

	//regionInfo
	struct regionInfoNode *oldRItmp = old->regionInfo;

	struct regionInfoNode *RItmp = NULL;
	struct regionInfoNode *RItmp2;
	while(oldRItmp != NULL){
		//RItmp2 init
		RItmp2 = (struct regionInfoNode*)kmalloc(sizeof(struct regionInfoNode));
		if(RItmp2 == NULL){
			as_destroy(newas);
			return ENOMEM;
		}
		RItmp2->as_vbase = oldRItmp->as_vbase;
		RItmp2->as_npages = oldRItmp->as_npages;
		RItmp2->as_permission = oldRItmp->as_permission;
		RItmp2->next = NULL;
		//link
		RItmp2->next = RItmp;
		RItmp = RItmp2;
		oldRItmp = oldRItmp->next;
	}
	newas->regionInfo = RItmp;

	*ret = newas;
	return 0;
}
