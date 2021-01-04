#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "../../include/cape.h"
#include "mpi.h"
#include <sys/mman.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

#if DDEBUG
#define dprintf(fmt, args...) printf(fmt, ## args);
#else
#define dprintf(fmt, args...) ;
#endif
/**********Local Variables ********************************************/        
static char *_var_data; //copy variable data
static unsigned long * __var_addr; //address of variables
static unsigned long * __var_len; //
static FILE * __ckpt_data;


PointerList *__var_heap_list_head = NULL;
PointerList *__var_heap_list_tail = NULL;

VarList *__active_variable_head = NULL;
VarList *__active_variable_tail = NULL;
VarList *__var_list_head = NULL;
VarList *__var_list_tail = NULL;

FILE *ckpt_data_stream;
FILE *before_ckpt_stream, *after_ckpt_stream, *final_ckpt_stream;
char *ckpt_data, *before_ckpt, *after_ckpt, *final_ckpt;
size_t ckpt_data_size, before_ckpt_size, after_ckpt_size, final_ckpt_size;

static unsigned char __activate_func_level__ = 1;
static unsigned char __parallel_level__ = 0;
static int __node__ = -1; //current node
static int __nnodes__ = -1 ; // Number of working nodes
static int __total_nodes__ = -1 ; //Total nodes in the system
static int __cape_token__ = -1;
static unsigned int __current_session__ = -1;

static __is_inside_parallel_region__ = 0;

char __ckpt_data_file[100];
int __ckpt_data_size = 0;

/**********Public Variables ********************************************/     
long long  __left__ = -1;
long long __right__ = -1;
long long  __i__;
unsigned long __time_stamp__ = 1 ;
unsigned long __pc__;
/***********************************************************************/


/**********************************************************************/
/* 					Private Functions								 */
/*********************************************************************/
 /*
  * Add active variables in __active_variable list
  * 	Insert at the end of list (LIFO)
  */
int add_active_variable(VarList **vlist_head, VarList **vlist_tail, Var v){	
	VarList *vl;	
	vl = malloc(sizeof(struct VarList));
	vl->next = NULL;
	vl->prev = NULL;
	vl->var.addr = v.addr;
	vl->var.size = v.size;
	vl->var.n = v.n;
	vl->var.pro = v.pro;
	vl->var.level = v.level;
	vl->var.dtype = v.dtype;
	vl->var.ispointer = v.ispointer;	
	if (*vlist_head == NULL){
		*vlist_head = vl ;
		*vlist_tail = vl;
		return 1;
	}	
	VarList *tmp2;
	tmp2 = *vlist_tail;	
	//if variable exists in list
	if (tmp2->var.addr == vl->var.addr){
		free(vl);
		return 0;
	}
	//insert at the end of list
	vl->prev = tmp2;
	tmp2->next = vl;
	*vlist_tail = vl;
	return 1;	
}
/*
 * Remove all variables with in var.level =  func_level from activate list
 * and remove all variables on heap that is managered by these variables
 */
int remove_active_variables(VarList **vlist_head, VarList **vlist_tail, unsigned char func_level){
	
	if (*vlist_head == NULL) 
		return 0 ;
	
	VarList *tmp1, *tmp2;
	tmp1 = *vlist_head;
	tmp2 = *vlist_tail;	
	while((tmp2 != tmp1) && (tmp2->var.level == func_level)){
		VarList *tmp;
		tmp = tmp2;		
		tmp2= tmp2->prev;
		tmp2->next = NULL;
		*vlist_tail = tmp2;
		if(tmp->var.ispointer)
			remove_heap_variables(&__var_heap_list_head,
								&__var_heap_list_tail,
								tmp->var.addr);
		free(tmp);
	}	
	if ((tmp1 == tmp2) && (tmp1->var.level == func_level)){
		*vlist_head = NULL;
		*vlist_tail = NULL;
		tmp2 = NULL;
		if(tmp1->var.ispointer)
			remove_heap_variables(&__var_heap_list_head,
								&__var_heap_list_tail,
								tmp1->var.addr);
		free(tmp1);
		return 0;
	}
	return 1;
}
/*
 * Generate variables list and their parallel level for parallel windows
 * Read variables form active variables list, group each variables is 4 bytes
 * If (list !EXISTS) create_new
 * else
 * 		copy_variable_with_new_level_of_parallel_region
 */
int generate_variable_list_for_parallel_windows(VarList *active_variable_head){
	unsigned int size, nelements, addr;
	if (__var_list_head == NULL){
		if (active_variable_head == NULL) return 0;
		VarList *tmp;
		tmp = active_variable_head;
		while(tmp!=NULL){
			Var var;		
			var.dtype = tmp->var.dtype;
			var.level = __parallel_level__;
			var.ispointer = tmp->var.ispointer;
			var.pro = tmp->var.pro;	

			///TODO: COPY and ADD variables in to new list (note group of 4 bytes)			
			if(tmp->var.size < CAPE_WORD){
				size = CAPE_WORD;
				addr = tmp->var.addr & ~(CAPE_WORD - 1);
				if (tmp->var.n % CAPE_WORD==0) 
					nelements = tmp->var.n / CAPE_WORD;
				else
					nelements = tmp->var.n / CAPE_WORD + 1;
			}else {		
				size = tmp->var.size;
				nelements = tmp->var.n;
				addr = tmp->var.addr ;
			}
			var.n = nelements ;
			var.addr = addr ;
			var.size = size;			
			//add var into new list
			add_shared_variable(&__var_list_head, &__var_list_tail, var);	
	
			tmp = tmp->next;
		}
	}
	else{
		__parallel_level__++;
		///TODO: Implement multi-level parallel region
		return 0;
	}	
}

/*
 * add shared variable in to __var_list_head
 */
int add_shared_variable(VarList **vlist, VarList **vlist_tail, Var var){
	VarList *vl;
	if (var.addr <= 0) return 0;		
	vl = malloc(sizeof(struct VarList));
	vl->next = NULL;
	vl->prev = NULL;
	vl->var.addr = var.addr ;
	vl->var.size = var.size ;
	vl->var.n = var.n;
	vl->var.pro = var.pro;
	vl->var.level=var.level;
	vl->var.dtype =var.dtype;	
	vl->var.ispointer = var.ispointer ;
	if (*vlist == NULL ) {
		*vlist = vl;
		*vlist_tail = vl;
		return 1;
	}
		
	VarList *tmp,*tmp2;
	tmp = *vlist;
	tmp2 = *vlist_tail;
	//Insert at the begin of list
	if (tmp->var.addr > vl->var.addr) {
		tmp->prev = vl;
		vl->next = tmp;
		*vlist = vl ;
		return 1;
	}	
	//Mofify the variable properties
	if (tmp->var.addr == vl->var.addr) {		
		free(vl);
		return 2;
	}	
	//Insert at the end of list
	if(tmp2->var.addr < vl->var.addr){
		tmp2->next = vl;
		vl->prev = tmp2;
		*vlist_tail = vl;
		return 1;
	}
	//Insert at the end of list
	if(tmp2->var.addr == vl->var.addr){
		free(vl);
		return 2;
	}
	
	//Find the position to insert or modify
	while ((tmp->next != NULL) && (tmp->var.addr < vl->var.addr )){
		tmp = tmp->next;
	}	
	//Insert before tmp
	if (tmp->var.addr > vl->var.addr){
		vl->next = tmp;
		vl->prev = tmp->prev;
		tmp->prev->next = vl;
		tmp->prev = vl ;
		return 1;
	}
	//Exist in list
	if (tmp->var.addr == vl->var.addr){
		free(vl);
		return 2;
	}	
} 

/*
 * --------------------------------------------------------------------
 * Add new pointer into heap list
 * 	manager_addr is not exists in heaplist
 * 	[addr, addr+len] is not exists in heaplist
 * --------------------------------------------------------------------
 */
int addnew_pointer(PointerList **hlist_head, 
		PointerList **hlist_tail, 
		Pointer pt){
	
	PointerList *item;
	item = malloc(sizeof(PointerList));
	item->pointer.manager_addr = pt.manager_addr;
	item->pointer.addr = pt.addr;
	item->pointer.len = pt.len ;
	item->next = NULL;
	item->prev = NULL;
	
	if (*hlist_head == NULL){
		*hlist_head = item ;
		*hlist_tail = item ;
		return 1 ;
	}
	
	PointerList *tmp_head, *tmp_tail;
	tmp_head = *hlist_head;
	tmp_tail = *hlist_tail;
	//insert at the begin of list
	if (tmp_head->pointer.addr < item->pointer.addr){
		item->next = tmp_head;
		tmp_head->prev = item;
		*hlist_head = item;
		return 1;
	}
	//insert at the end of list
	if (tmp_tail->pointer.addr > item->pointer.addr){
		tmp_tail->next = item ;
		item->prev = tmp_tail ;
		*hlist_tail = item ;
		return 1;
	}
	
	//find location to insert
	while((tmp_head !=NULL) && (tmp_head->pointer.addr < item->pointer.addr))
		tmp_head = tmp_head->next;
	
	//insert before tmp_head
	item->next = tmp_head;
	item->prev = tmp_head->prev;
	tmp_head->prev->next = item ;
	tmp_head->prev = item;
	return 1;	
}
/*
 * --------------------------------------------------------------------
 * Remove head variables
 * Parameters:
 * 	manager address
 * Return
 * 	Head list pointers, that remove the item managered by manager_addr
 * -------------------------------------------------------------------
 */

int remove_heap_variables(PointerList **hlist_head, 
		PointerList **hlist_tail,
		unsigned long manager_addr){	

	if (*hlist_head == NULL) return 0;
	
	PointerList *tmp_head;
	PointerList *tmp_tail;
	PointerList *tmp;
	
	tmp_head = *hlist_head;
	tmp_tail = *hlist_tail;
	
	if ((tmp_head == tmp_tail) && (tmp_head->pointer.manager_addr == manager_addr)){
		*hlist_head = NULL;
		*hlist_tail = NULL;
		free(tmp_head);
		return 1;
	}	
	tmp= tmp_head;
	while(tmp != NULL){
		if ((tmp->pointer.manager_addr == manager_addr) && (tmp==tmp_head)){
			tmp_head = tmp->next;
			tmp->next = NULL;
			tmp_head->prev = NULL;
			free(tmp);
			*hlist_head = tmp_head;
			return 1;
		}
		if ((tmp->pointer.manager_addr == manager_addr) && (tmp==tmp_tail)){
			tmp_tail = tmp->prev;
			tmp->prev = NULL;
			tmp_tail->next = NULL;
			free(tmp);
			*hlist_tail = tmp_tail;
			return 1;
		}
		if (tmp->pointer.manager_addr == manager_addr){
			tmp->prev->next = tmp->next;
			tmp->next->prev = tmp->prev;
			tmp->prev = NULL;
			tmp->next = NULL;
			free(tmp);
			return 2;
		}		
		tmp = tmp->next;
	}
	
	
}

/*
 * ---------------------------------------------------------------------
 * Remove item contain manager_addr or addr in heaplist
 * ---------------------------------------------------------------------
 */
int remove_exists_item(PointerList **hlist_head, 
		PointerList **hlist_tail, 
		Pointer pt){	
	if (*hlist_head == NULL) return 0;
	PointerList *tmp_head;
	PointerList *tmp_tail;
	PointerList *tmp;
	
	tmp_head = * hlist_head;
	tmp_tail = * hlist_tail;
	
	while (tmp_head != *hlist_tail){
		if ((tmp_head->pointer.manager_addr == pt.manager_addr) ||
				((tmp_head->pointer.addr >=pt.addr) 
						&& (tmp_head->pointer.addr <= pt.addr + pt.len) ) ||
				((tmp_head->pointer.addr+tmp_head->pointer.len >= pt.addr) 
						&& (tmp_head->pointer.addr+tmp_head->pointer.len <= pt.addr +pt.len ) )
			){
			tmp = tmp_head ;
			if (tmp_head = *hlist_head){					
				tmp_head = tmp_head->next;
				*hlist_head = tmp_head;							
			}else {
				tmp_head->prev->next = tmp->next;
				tmp_head->next->prev = tmp->prev ;
				tmp_head = tmp->next ;	
			}
			free(tmp);			
		} else{
		
			tmp_head = tmp_head->next;
		}
	}
	
	// Last node
	if ((tmp_head->pointer.manager_addr == pt.manager_addr) ||
				((tmp_head->pointer.addr >=pt.addr) 
						&& (tmp_head->pointer.addr <= pt.addr + pt.len) ) ||
				((tmp_head->pointer.addr+tmp_head->pointer.len >= pt.addr) 
						&& (tmp_head->pointer.addr+tmp_head->pointer.len <= pt.addr +pt.len ) )
			){
	
		*hlist_head = NULL;
		*hlist_tail = NULL;
		free (tmp_head) ;
	}
	return 1 ;	
}


/*
 * ---------------------------------------------------------------------
 * Open the window of variables
 * ---------------------------------------------------------------------
 */
void open_parallel_window(){	
	__is_inside_parallel_region__ = TRUE; // Enter parallel reion	
	__parallel_level__ ++;
	generate_variable_list_for_parallel_windows(__active_variable_head);	
}

/*
 * ---------------------------------------------------------------------
 * Close the window of variables
 * Release checkpoint memory
 * ---------------------------------------------------------------------
 */
void close_parallel_window(){
	
	if (__parallel_level__ > 0) {
		remove_active_variables(&__var_list_head, &__var_list_tail, __parallel_level__);		
		__parallel_level__--;
	}
	__is_inside_parallel_region__= FALSE; //Exit parallel region

}

/*
 * ---------------------------------------------------------------------
 * Add new region in parallel region
 * => copy variables of parent's level
 * ---------------------------------------------------------------------
 */
int add_parallel_region(VarList **vlist_head, VarList **vlist_tail, unsigned char level){
	
	VarList *vl_tail_level = NULL;
	VarList *vl_head = NULL;
	
	vl_head = *vlist_head;
	vl_tail_level = *vlist_tail;
	
	if(vl_head == NULL) return 0;
	VarList *copy_item = NULL;		
	VarList *item = NULL;
	
	item = vl_head;
	while(item->var.level != (level - 1) )
		item = item->next;
	
	//Copy items and assigned new level	
	while(item->var.level == (level -1)){			
		copy_item = malloc(sizeof(VarList));		
		copy_item->var.addr = item->var.addr;
		copy_item->var.size = item->var.size;
		copy_item->var.n = item->var.n;
		copy_item->var.dtype = item->var.dtype;
		copy_item->var.pro = item->var.pro; 
		copy_item->var.level = level; //New level	
		copy_item->var.ispointer = item->var.ispointer;		
		copy_item->next = NULL;
		copy_item->prev = vl_tail_level;		
		vl_tail_level->next = copy_item;
		vl_tail_level = copy_item;		
		item = item->next;
	}	
	*vlist_tail = vl_tail_level;
	return 1;
}
/*
 * ---------------------------------------------------------------------
 * Remove variables level
 *----------------------------------------------------------------------
 */
int remove_parellel_region(unsigned char level){
	if( level > 1){
		remove_active_variables(&__var_list_head, &__var_list_tail, level ); 
	}
	return 1;
}
/*
 * ---------------------------------------------------------------------
 * Find variable in VarList with address addr * 
 * ---------------------------------------------------------------------
 */
VarList * find_variable_by_addr(VarList *vlist, long addr, char level){
	VarList *vl = NULL;
	vl = vlist;	
	if (vl == NULL) return vl;
	while (vl != NULL) {
		if ((vl->var.addr == addr) && (vl->var.level == level ) ){				 
			break;
		}
		//printf("Checking: Ox%lx is in VarList: [Ox%lx - Ox%lx) - Level: %d \n" , addr, vl->var.addr, vl->var.addr + (vl->var.n * vl->var.size), vl->var.level);
		vl = vl->next;		
	}
	return vl;	
}

/*
 * ---------------------------------------------------------------------
 * Generate checkpoint 
 * 	Checkpoint will be generated depending on properties of variables 
 * 	and the phase of execution model.
 * Checkpoint Structure:
 * C = {t , program counter, size_of_S, S, L}, 
 * 		where S = All modified data (addr, len, data)
 * 			  L = data of reduction variables (addr, len, data)
 *----------------------------------------------------------------------
 */
FILE *generate_checkpoint(VarList *vlist,	
			unsigned char level,
			unsigned char cflag,
			unsigned char ops_flag,
			unsigned long tsp,
			unsigned long pc ){
	
	VarList *v = NULL, *v1 = NULL;	
	unsigned long timestamp, programcounter;
	unsigned long size_s = 0;
	unsigned long start_addr;
	unsigned long  v_end, v_addr;
	unsigned long start;
	unsigned int len;
	unsigned long file_pointer = 0;
	char *data;
	
		
	FILE *stream;
	
	//open checkpoint file
	timestamp = tsp;
	stream = open_memstream(&after_ckpt, &after_ckpt_size);
	
	//write time stamp into checkpoint file
	fwrite(&timestamp, sizeof(unsigned long), 1, stream);
	fflush(stream);
	
	//write program counter into checkpoint file
	programcounter = pc ;
	fwrite(&programcounter, sizeof(unsigned long), 1, stream);
	fflush(stream);
	
	if (ckpt_data_size <= 0){ 
		size_s = 12;
		fwrite(&size_s, sizeof(unsigned long), 1, stream);
		fflush(stream);		
		return stream;
	}
	
	//Write size_s into checkpoint file, and this place will be modified after writting S part
	fwrite(&size_s, sizeof(unsigned long), 1, stream);
	fflush(stream);
	
	//Move to current window (the current level of VarList)
	v = vlist;
	while ((v != NULL) && (v->var.level != level))
		v = v->next;
		
	//Move to the first DPAGE
	//while ((v != NULL) && ((v->addr + v->n * v->size) < dplist->addr))
	//	v = v->next;
	unsigned int data_pointer;	
	while(file_pointer < ckpt_data_size){
		v_addr = (*(unsigned long *) (ckpt_data + file_pointer)) ; 		
		v1 = find_variable_by_addr(v, v_addr, level);
		file_pointer += sizeof(unsigned long);
		data_pointer = file_pointer; //save possition of data
		file_pointer += v1->var.n * v1->var.size ;
		
		if ((v1->var.pro == CAPE_PRIVATE) || (v1->var.pro == CAPE_THREAD_PRIVATE))
			continue;
		
		if ((cflag == ENTRY_CHECKPOINT) && (v1->var.pro == CAPE_LAST_PRIVATE))
			continue;
		
		if ((cflag ==EXIT_CHECKPOINT) && 
			((v1->var.pro == CAPE_FIRST_PRIVATE) ||
		     (v1->var.pro == CAPE_COPY_IN) ||
		     (v1->var.pro == CAPE_SUM) ||
		     (v1->var.pro == CAPE_MUL) || 
		     (v1->var.pro == CAPE_MAX) || 
		     (v1->var.pro == CAPE_MIN)))
				continue ;
			
	//	printf("CHECKPOINT DATA: In VarList: [Ox%lx - Ox%lx) - Level: %d - %d bytes \n" , \
			v1->var.addr, v1->var.addr + (v1->var.n * v1->var.size), v1->var.level , v1->var.n * v1->var.size);
		
		start_addr = v_addr;
		v_end = v_addr + (v1->var.n * v1->var.size);
		while (start_addr < v_end){
			if (v1->var.size == CAPE_WORD){
				//Ignore the data that is not modified
				while ((start_addr < v_end) &&						
					( (*(int *)start_addr) == (*(int *) (ckpt_data + data_pointer + (start_addr - v_addr ))) )	)			
					start_addr += CAPE_WORD;				
				//count the modified data
				start = start_addr ;
				while ((start_addr < v_end) &&						
					( (*(int *)start_addr) != (*(int *) (ckpt_data + data_pointer + (start_addr - v_addr ))) )	)
				{			
					//printf("(%d) \t" , (*(int *) start_addr) );
					start_addr += CAPE_WORD ;
				}
				//save into ckpt file
				if (start_addr > start){
					len = start_addr - start ;
					fwrite(&start, sizeof(unsigned long), 1, stream);
					fwrite(&len, sizeof(unsigned long), 1, stream);
					fwrite(start, len, 1, stream);
					fflush(stream);
				}				
			}
			else{ //8 bytes			
				//Ignore the data that is not modified
				while ((start_addr < v_end) &&						
					( (*(double*)start_addr) == (*(double *) (ckpt_data + data_pointer + (start_addr - v_addr ))) )	)			
					start_addr += DOUBLE_CAPE_WORD;				
				//count the modified data
				start = start_addr ;
				while ((start_addr < v_end) &&						
					( (*(double *)start_addr) != (*(double *) (ckpt_data + data_pointer + (start_addr - v_addr ))) )	)
				{			
					//printf("%d \t" , (*(int *) start_addr) );
					start_addr += DOUBLE_CAPE_WORD ;
				}
				//save into ckpt file
				if (start_addr > start){
					len = start_addr - start ;
					fwrite(&start, sizeof(unsigned long), 1, stream);
					fwrite(&len, sizeof(unsigned long), 1, stream);
					fwrite(start, len, 1, stream);
					fflush(stream);
				}			
			}
		
			//printf("\nNode %d: File pointer %ld - file size %ld \n",__node__, file_pointer, ckpt_data_size);
		}
	}	
	//Identity size of S part
	size_s = after_ckpt_size;		
	//Write L part into checkpoint
	if((cflag == EXIT_CHECKPOINT) && (ops_flag==TRUE)){
		//Move to current window (the current level of VarList)
		v = vlist;
		while ((v != NULL) && (v->var.level != level))
			v = v->next;
		while ((v != NULL) && (v->var.level == level)){
			if ((v->var.pro == CAPE_SUM) || (v->var.pro == CAPE_MUL) || (v->var.pro == CAPE_MAX) ||(v->var.pro == CAPE_MIN) ){
				len = v->var.size * v->var.n;
				fwrite(&v->var.addr, sizeof(long), 1, stream);
				fwrite(&len, sizeof(unsigned int), 1, stream);
				fwrite(v->var.addr, len, 1, stream);
				fflush(stream);				
			}
			v = v->next;
		}			
	}
	memcpy((after_ckpt + 2*sizeof(unsigned long)), &size_s, sizeof(unsigned int));		
	
	return stream;

}

/**
 * ---------------------------------------------------------------------
 * Merge S = S1 + S2
 * Write S into after_checkpoint
 * Structer of S part is: {(addr, len, data) .... }
 * --------------------------------------------------------------------- 
 */
int merge_data(char *s1, unsigned int pos_s1, unsigned size_s1, 
				char *s2, unsigned int pos_s2, unsigned size_s2){
	unsigned int p1, p2;
	unsigned int len, len1, len2;
	long addr1, addr2, old_addr2;
	p1 = pos_s1;
	p2 = pos_s2;
	
	//printf("*** NODE %d: Position 1= %d, possition 2 =%d: Merge %ld bytes s1 and %ld bytes s2 \n", __node__, pos_s1, pos_s2, size_s1, size_s2);
	
	if ((p1 >= size_s1) && (p2 >=size_s2))
		return 0;
	//if S1 == NULL =>  S = S2
	if (p1 >= size_s1 ){
		fwrite(s2 + p2, size_s2 - p2, 1, after_ckpt_stream);
		fflush(after_ckpt_stream);
		return 1;
	}
	//if S2 == NULL => S = S1
	if (p2 >= size_s2 ){
		fwrite(s1 + p1, size_s1 - p1, 1, after_ckpt_stream);
		fflush(after_ckpt_stream);
		return 1;
	}		
	addr1 = *(long *) (s1 + p1 );
	p1 += sizeof(long);
	len1 = *(unsigned int *) (s1 + p1) ;
	p1 += sizeof(unsigned int);
	
	addr2 = *(long *) (s2 + p2 );
	p2 += sizeof(long);
	len2 = *(unsigned int *) (s2 + p2) ;
	p2 += sizeof(unsigned int);
	while ((p1 < size_s1) && (p2 < size_s2)){
		//printf("\n Node %ld: (0x%lx - %ld ) + (0x%lx - %ld)", __node__, addr1, len1, addr2, len2) ;
		if (addr1 <= addr2){
			fwrite(&addr1, sizeof(long), 1, after_ckpt_stream);
			fflush(after_ckpt_stream);
			fwrite(&len1, sizeof(unsigned int), 1, after_ckpt_stream);
			fflush(after_ckpt_stream);			
			fwrite(s1 + p1, len1, 1, after_ckpt_stream);
			fflush(after_ckpt_stream);
			p1 += len1;
			//printf("\n Node %ld: Write: 0x%lx  : %ld ", __node__, addr1, len1) ;
			//S1: [-----------)  
			//S2:     -----		 ------	
			if ((addr1 + len1) >= (addr2+ len2)){
				p2 += len2;
				if (p2 < size_s2) {				
					addr2 = *(long *) (s2 + p2 );
					p2 += sizeof(long);
					len2 = *(unsigned int *) (s2 + p2) ;
					p2 += sizeof(unsigned int);
				}
			}			
			//S1: [-----------)       [--------)
			//S2:        -----[----)
			if (((addr1 + len1) >= addr2) && ((addr1 + len1) < (addr2 + len2))){
				old_addr2 = addr2; //save the old addr
				addr2 = addr1 + len1;
				len2 = len2 - (addr2 - old_addr2);
				p2 += (addr2 - old_addr2);			
			}			
			if (p1 < size_s1) {
				addr1 = *(long *) (s1 + p1 );
				p1 += sizeof(long);
				len1 = *(unsigned int *) (s1 + p1) ;
				p1 += sizeof(unsigned int);
			}					
		}else{ //addr1 > addr2
			//S1:                      [-----------)         [--------)
			//S2:        [--------)		    -------[------)	
			if ((addr2 + len2) < addr1) {
				fwrite(&addr2, sizeof(long), 1, after_ckpt_stream);
				fflush(after_ckpt_stream);
				fwrite(&len2, sizeof(unsigned int), 1, after_ckpt_stream);
				fflush(after_ckpt_stream);			
				fwrite(s2 + p2, len2, 1, after_ckpt_stream);
				fflush(after_ckpt_stream);
				//printf("\n Node %ld: Write: 0x%lx : %ld ", __node__, addr2, len2) ;
				p2 += len2;					
				if (p2 >= size_s2) break;
				addr2 = *(long *) (s2 + p2 );
				p2 += sizeof(long);
				len2 = *(unsigned int *) (s2 + p2) ;
				p2 += sizeof(unsigned int);												
			//S1:       [-----------)        [--------)
			//S2:       	 -------[--------)--------[------)	
			}else {
				len = addr1 - addr2;				
				fwrite(&addr2, sizeof(long), 1, after_ckpt_stream);
				fflush(after_ckpt_stream);				
				fwrite(&len, sizeof(unsigned int), 1, after_ckpt_stream);
				fflush(after_ckpt_stream);
				fwrite(s2 + p2, len2, 1, after_ckpt_stream);
				fflush(after_ckpt_stream);
				//printf("\n Node %ld: Write: 0x%lx : %ld ", __node__, addr2, len2) ;
				p2 += len;

				len2 = len2-len;	
				addr2 = addr1;							
			}
		}	
	}	
	//Merge the rest part
	if (p1 < size_s1){
		fwrite(&addr1, sizeof(long), 1, after_ckpt_stream);
		fflush(after_ckpt_stream);
		fwrite(&len1, sizeof(unsigned int), 1, after_ckpt_stream);
		fflush(after_ckpt_stream);			
		fwrite(s1 + p1, len1, 1, after_ckpt_stream);
		fflush(after_ckpt_stream);
		p1 += len1;
	}	
	while (p1 < size_s1){
		addr1 = *(long *) (s1 + p1 );
		p1 += sizeof(long);
		len1 = *(unsigned int *) (s1 + p1) ;
		p1 += sizeof(unsigned int);
		fwrite(&addr1, sizeof(long), 1, after_ckpt_stream);
		fflush(after_ckpt_stream);
		fwrite(&len1, sizeof(unsigned int), 1, after_ckpt_stream);
		fflush(after_ckpt_stream);			
		fwrite(s1 + p1, len1, 1, after_ckpt_stream);
		fflush(after_ckpt_stream);
		p1 += len1;		
	}
	
	if (p2 < size_s2){
		fwrite(&addr2, sizeof(long), 1, after_ckpt_stream);
		fflush(after_ckpt_stream);
		fwrite(&len2, sizeof(unsigned int), 1, after_ckpt_stream);
		fflush(after_ckpt_stream);			
		fwrite(s2 + p2, len2, 1, after_ckpt_stream);
		fflush(after_ckpt_stream);
		p2 += len2;
	}	
	while (p2 < size_s2){
		addr2 = *(long *) (s2 + p2 );
		p2 += sizeof(long);
		len2 = *(unsigned int *) (s2 + p2) ;
		p2 += sizeof(unsigned int);
		fwrite(&addr2, sizeof(long), 1, after_ckpt_stream);
		fflush(after_ckpt_stream);
		fwrite(&len2, sizeof(unsigned int), 1, after_ckpt_stream);
		fflush(after_ckpt_stream);			
		fwrite(s2 + p2, len2, 1, after_ckpt_stream);
		fflush(after_ckpt_stream);
		p2 += len2;		
	}
	return 2;	
}
/*
 * ---------------------------------------------------------------------
 * Merge a buffer to after_checkpoint
 * 	if (t1 >= t2) 
 * 		{ C <- t1 , pc1, size_s, S1 + S2}
 * 	else
 * 		{ C <- t2, pc2, size_s, S2+S1}
 * --------------------------------------------------------------------- 
 */
int merge_checkpoint(char *src_ckpt, size_t src_size, char ckpt_flag){	
	
	FILE *tmp_stream;
	char *tmp_ckpt;
	size_t tmp_size;
	
	unsigned int src_pointer =0, tmp_pointer =0;
	
	if (src_size <= 4)
		return 0;
	
	if (after_ckpt_size == 0){	
		after_ckpt_stream = open_memstream(&after_ckpt, &after_ckpt_size);
		fwrite(src_ckpt, src_size, 1, after_ckpt_stream);
		fflush(after_ckpt_stream);
		return src_size;	
	}	
	//Copy des_stream file to tmp_stream file
	tmp_stream = open_memstream(&tmp_ckpt, &tmp_size);
	fwrite(after_ckpt, after_ckpt_size, 1,tmp_stream);
	fflush(tmp_stream);
	
	//Close des_stream file
	fclose(after_ckpt_stream);
	free(after_ckpt);
	after_ckpt = NULL;
	after_ckpt_size = 0;
		
	src_pointer = 0;
 	tmp_pointer =0 ;
 	unsigned long t1, t2;
 	unsigned long pc1, pc2;
 	unsigned long size_s = 0, size_s1, size_s2;
  	
	
 	t1 = *(unsigned long *)tmp_ckpt;
 	t2 = *(unsigned long *)src_ckpt;  	 	
 	tmp_pointer += sizeof(unsigned long);
 	src_pointer += sizeof(unsigned long);
 	
 	pc1 =  *(unsigned long *)(tmp_ckpt + tmp_pointer);
 	pc2 =  *(unsigned long *)(src_ckpt + src_pointer);	 	
 	src_pointer += sizeof(unsigned long);
 	tmp_pointer += sizeof(unsigned long);
 	
 	size_s1 =  *(unsigned long *)(tmp_ckpt + tmp_pointer);
 	size_s2 =  *(unsigned long *)(src_ckpt + src_pointer);
 	 	
 	src_pointer += sizeof(unsigned long);
 	tmp_pointer += sizeof(unsigned long);
	 	
 	//Open des_stream file again
 	after_ckpt_stream = open_memstream(&after_ckpt, &after_ckpt_size);
 	
 	
 	if (t1 >= t2){ 			//C <- t1, pc1, size_s, S1+ S2		
		fwrite(&t1, sizeof(unsigned long), 1, after_ckpt_stream);
		fflush(after_ckpt_stream);
		fwrite(&pc1, sizeof(unsigned long), 1, after_ckpt_stream);
		fflush(after_ckpt_stream);
		fwrite(&size_s, sizeof(unsigned long), 1, after_ckpt_stream);
		fflush(after_ckpt_stream);
		merge_data(tmp_ckpt, tmp_pointer, size_s1, src_ckpt, src_pointer, size_s2);		
	}else{	//C <-	t2, pc2, size_s, S2 + S1		
		fwrite(&t2, sizeof(unsigned long), 1, after_ckpt_stream);
		fflush(after_ckpt_stream);
		fwrite(&pc2, sizeof(unsigned long), 1, after_ckpt_stream);
		fflush(after_ckpt_stream);
		fwrite(&size_s, sizeof(unsigned long), 1, after_ckpt_stream);
		fflush(after_ckpt_stream);		
		merge_data(src_ckpt, src_pointer, size_s2, tmp_ckpt, tmp_pointer, size_s1);				
	}
	tmp_pointer = size_s1;
	src_pointer = size_s2;	
	size_s = after_ckpt_size;
	
	//Check if exist reduction data
	if (ckpt_flag == EXIT_CHECKPOINT){ 
		while (	(tmp_pointer < tmp_size) && (src_pointer <src_size)){		
			long addr;
			unsigned int len;
			
			addr = *((long*)(tmp_ckpt + tmp_pointer));
			src_pointer += sizeof(long);
			tmp_pointer += sizeof(long);

			len = *(unsigned int *) (tmp_ckpt + tmp_pointer) ;
			src_pointer += sizeof(long);
			tmp_pointer += sizeof(long);
			
			fwrite(&addr, sizeof(long), 1, after_ckpt_stream);
			fflush(after_ckpt_stream);
			fwrite(&len, sizeof(unsigned int), 1, after_ckpt_stream);
			fflush(after_ckpt_stream);

			VarList *var = NULL; 
			var = find_variable_by_addr(__var_list_head, addr , __parallel_level__);	
			
			if (var == NULL) return -1; //ERROR

			int num, n1, n2;
			unsigned long num_l, n1_l, n2_l;
			float num_f, n1_f, n2_f;
			double num_d, n1_d, n2_d;			
			switch (var->var.dtype){
				case CAPE_CHAR:
				case CAPE_INT:
				case CAPE_LONG:					
					n1 =  *(long*) (tmp_ckpt + tmp_pointer);
					n2=  *(long*) (src_ckpt + src_pointer) ;
					if (var->var.pro == CAPE_SUM){
						num = 0;
						num = n1 + n2;
						fwrite(&num, len, 1, after_ckpt_stream);
						fflush(after_ckpt_stream);
						break;						
					}
					if (var->var.pro == CAPE_MUL){
						num = 1;
						num = n1 * n2;		
						fwrite(&num, len, 1, after_ckpt_stream);
						fflush(after_ckpt_stream);
						break;						
					}					
					if (var->var.pro == CAPE_MAX){
						num = (n1 >= n2 )? n1 : n2 ;
						fwrite(&num, len, 1, after_ckpt_stream);
						fflush(after_ckpt_stream);
						break;						
					}
					if (var->var.pro == CAPE_MIN){						
						num = (n1 < n2 )? n1 : n2 ;				
						fwrite(&num, len, 1, after_ckpt_stream);
						fflush(after_ckpt_stream);
						break;						
					}							
					break;
				case CAPE_UNSIGNED_CHAR:	
				case CAPE_UNSIGNED_INT:
				case CAPE_UNSIGNED_LONG:					
					n1_l =  *(unsigned long*) (tmp_ckpt + tmp_pointer);
					n2_l=  *(unsigned long*) (src_ckpt + src_pointer) ;
					if (var->var.pro == CAPE_SUM){
						num_l = 0;
						num_l = n1_l + n2_l;
						fwrite(&num_l, len, 1, after_ckpt_stream);
						fflush(after_ckpt_stream);
						break;						
					}
					if (var->var.pro == CAPE_MUL){
						num_l = 1;
						num_l = n1_l * n2_l;		
						fwrite(&num_l, len, 1, after_ckpt_stream);
						fflush(after_ckpt_stream);
						break;						
					}					
					if (var->var.pro == CAPE_MAX){
						num_l = (n1_l >= n2_l )? n1_l : n2_l ;
						fwrite(&num_l, len, 1, after_ckpt_stream);
						fflush(after_ckpt_stream);
						break;						
					}
					if (var->var.pro == CAPE_MIN){						
						num_l = (n1_l < n2_l )? n1_l : n2_l ;				
						fwrite(&num_l, len, 1, after_ckpt_stream);
						fflush(after_ckpt_stream);
						break;						
					}							
					break;
				case CAPE_FLOAT:					
					n1_f =  *(float*) (tmp_ckpt + tmp_pointer);
					n2_f=  *(float*) (src_ckpt + src_pointer) ;
					if (var->var.pro == CAPE_SUM){
						num_f = 0.0;
						num_f = n1_f + n2_f;
						fwrite(&num_f, len, 1, after_ckpt_stream);
						fflush(after_ckpt_stream);
						break;						
					}
					if (var->var.pro == CAPE_MUL){
						num_f = 1.0;
						num_f = n1_f * n2_f;		
						fwrite(&num_f, len, 1, after_ckpt_stream);
						fflush(after_ckpt_stream);
						break;						
					}					
					if (var->var.pro == CAPE_MAX){
						num_f = (n1_f >= n2_f )? n1_f : n2_f ;
						fwrite(&num_f, len, 1, after_ckpt_stream);
						fflush(after_ckpt_stream);
						break;						
					}
					if (var->var.pro == CAPE_MIN){						
						num_f = (n1_f < n2_f )? n1_f : n2_f ;				
						fwrite(&num_f, len, 1, after_ckpt_stream);
						fflush(after_ckpt_stream);
						break;						
					}							
					break;
				case CAPE_DOUBLE:					
					n1_d =  *(double*) (tmp_ckpt + tmp_pointer);
					n2_d=  *(double*) (src_ckpt + src_pointer);					
					if (var->var.pro == CAPE_SUM){
						num_d = 0.0;
						num_d = n1_d + n2_d;
						fwrite(&num_d, len, 1, after_ckpt_stream);
						fflush(after_ckpt_stream);
						break;						
					}
					if (var->var.pro == CAPE_MUL){
						num_d = 1.0;
						num_d = n1_d * n2_d;		
						fwrite(&num_d, len, 1, after_ckpt_stream);
						fflush(after_ckpt_stream);
						break;						
					}					
					if (var->var.pro == CAPE_MAX){
						num = (n1_d >= n2_d )? n1_d : n2_d ;
						fwrite(&num_d, len, 1, after_ckpt_stream);
						fflush(after_ckpt_stream);
						break;						
					}
					if (var->var.pro == CAPE_MIN){						
						num_d = (n1_d < n2_d )? n1_d : n2_d ;				
						fwrite(&num_d, len, 1, after_ckpt_stream);
						fflush(after_ckpt_stream);
						break;						
					}							
					break;
				default:
					printf("This datatype is not supported !!!!!");
					return -1;
			}
			
			src_pointer += len;
			tmp_pointer += len;
		}
		

	}	
	memcpy(after_ckpt + (2*sizeof(unsigned long)), &size_s, sizeof(unsigned long) );		

	fclose(tmp_stream);
	free(tmp_ckpt);	
	tmp_ckpt = NULL;
	
	return src_size;	
}

/*----------------------------------------------------------------------
 * log2(n): calculate log2 of n
 *----------------------------------------------------------------------
 */
int mylog2(unsigned int n){
	int p = 0;
	int tmp;
	tmp = n;
	while(tmp >>=1) ++p;
	return p;	
}
/*----------------------------------------------------------------------
 * Check a number is power of 2 or not
 *---------------------------------------------------------------------
 */
int is_power_of_two(unsigned int n)
{
  if (n == 0) return 0;
  while (n != 1){
    if (n%2 != 0) return 0;
    n = n/2;
  }
  return 1;
}

/*----------------------------------------------------------------------
 * find the number lower than n, but it is nearest number that powerof 2
 * ---------------------------------------------------------------------
 */
unsigned int nearest_power_of_two(unsigned int n){
	
	if (is_power_of_two(n)) return n;
	while(n > 1){
		n--;
		if (is_power_of_two(n)) return n;
	}
	return 0;
}

/*
 *---------------------------------------------------------------------
 * Send and Receive checkpoint based on hypercube algorithm  
 *----------------------------------------------------------------------
 */
int hypercube_allreduce(unsigned int node, unsigned int num_nodes, char ckpt_flag){
	int i, nsteps = 0;
	unsigned int partner;
	unsigned long send_msg_size=0, recv_msg_size = 0;
	char *send_msg;
	char *recv_msg;
	MPI_Status status;
	
	nsteps = mylog2(num_nodes);
	
	send_msg = after_ckpt;
	send_msg_size = after_ckpt_size;	
	
	
	for(i = 0; i< nsteps ; i++){
		partner = node ^ (1 << i);
		
		MPI_Sendrecv(&send_msg_size, 1, MPI_UNSIGNED_LONG, partner, i,	\
					&recv_msg_size, 1, MPI_UNSIGNED_LONG, partner, i,	\
					MPI_COMM_WORLD, &status);
	
		
		recv_msg = malloc(sizeof(char) * recv_msg_size);
		
		MPI_Sendrecv(send_msg, send_msg_size, MPI_CHAR, partner, i,	\
					recv_msg, recv_msg_size, MPI_CHAR, partner, i, \
					MPI_COMM_WORLD, &status);
				
		merge_checkpoint(recv_msg, recv_msg_size, ckpt_flag);	
		
		free(recv_msg);
		recv_msg_size = 0;
		
		send_msg = after_ckpt;
		send_msg_size = after_ckpt_size;			
	
	}
	
	//printf("Node %d: After merge: %ld bytes \n",__node__, after_ckpt_size);
	
	return 1;
}

/*
 *---------------------------------------------------------------------
 * Send and Receive checkpoint based on Ring algorithm  
 *----------------------------------------------------------------------
 */
int ring_allreduce(unsigned int node, unsigned int nnodes, char ckpt_flag){

	char *send_msg;
	char *recv_msg;
	unsigned int send_msg_size, recv_msg_size;
	MPI_Status status;
	
	unsigned int i, left, right;
	left = (node - 1 + nnodes) % nnodes;
	right = (node + 1) % nnodes;
	
	send_msg = after_ckpt;
	send_msg_size = after_ckpt_size;
	
	for(i = 1 ; i < nnodes; i ++){		
		MPI_Sendrecv(&send_msg_size, 1, MPI_INT, right, i, 		\
						&recv_msg_size, 1, MPI_INT, left, i, 	\ 
						MPI_COMM_WORLD, &status) ;
		
		recv_msg = malloc(sizeof(char) * recv_msg_size );
		
		MPI_Sendrecv(send_msg, send_msg_size, MPI_CHAR, right, i,		\
						recv_msg, recv_msg_size, MPI_CHAR, left, i,		\
						MPI_COMM_WORLD, &status);
		
		merge_checkpoint(recv_msg, recv_msg_size, ckpt_flag);
		
		send_msg = recv_msg;
		send_msg_size = recv_msg_size;
		
		recv_msg = NULL;
		recv_msg_size = 0;				
	}
	return 1;
}

/*
 * ---------------------------------------------------------------------
 * Require synchronize checkpoints
 * ---------------------------------------------------------------------
 */

int require_allreduce(char ckpt_flag){
	
	//printf("Node %d:  nnodes = %d - is_power_of2: %d \n", __node__, __nnodes__, is_power_of_two(__nnodes__));
	
	if (after_ckpt_size == 0) return 0 ;	
	if (is_power_of_two(__nnodes__))
		hypercube_allreduce(__node__, __nnodes__, ckpt_flag);
	else
		ring_allreduce(__node__, __nnodes__, ckpt_flag);
	
}


/*
 * ---------------------------------------------------------------------
 * Inject checkpoint into application's memory
 * Parameters: checkpoint file
 * ---------------------------------------------------------------------
 */
int inject_checkpoint(char *data_ckpt, size_t file_size){
	unsigned long len=0, file_pointer = 0;
	long addr;
	
	if (file_size <= sizeof(unsigned long)*3) return 0;	
	//printf("\n Node %d: inject ckpt - file size = %d bytes", __node__, file_size);
	
	__pc__ = *(unsigned long *) (data_ckpt+ 4); //get program counter
	file_pointer = sizeof(unsigned long) * 3; //timestime, program counter, size_s	
	
	while(file_pointer < file_size){
		
		addr = *(long *) (data_ckpt + file_pointer);		
		file_pointer += sizeof(long);		
		
		len = *(unsigned int*) (data_ckpt + file_pointer );
		file_pointer += sizeof(long);			
		
		memcpy(addr, data_ckpt+file_pointer, len)	;		
		
		//if (__node__ == 0)
		//	printf("DATA: Node %d: addr = 0x%lx - len = %d bytes - data %d \n",__node__, addr, len, *(int *) (data_ckpt+ file_pointer) );	
				
		file_pointer += len ;			
	}
	
	return 1;
}

void release_checkpoint(){
	
//	if (after_ckpt_stream != NULL){
//		fclose(after_ckpt_stream);
//	}
	free(after_ckpt);	
	after_ckpt = NULL;
	after_ckpt_size = 0;
}


/***********************************************************************/
/* Runtime functions												   */
/***********************************************************************/
void cape_set_num_nodes(int nnodes){
	__nnodes__ = nnodes ;
}

void cape_set_time_stamp(int time_stamp){
	__time_stamp__ = time_stamp;
}

int cape_get_node_num(){
	return __node__;
}

int cape_get_num_nodes(){
	return __nnodes__;
}

long long cape_get_left(long long n, long long start){
	long long left = __node__ * (n - start) /__nnodes__ + start;
	return left ;
}

long long cape_get_right(long long n, long long start){
	long long right = (__node__ + 1) * (n - start) / __nnodes__ + start;	
	return right;
}

int cape_get_token(){
	return __cape_token__;
}

int cape_section(){
	__current_session__ ++ ; //declere section
	if (__current_session__ % __nnodes__ == __node__)
		return 1;
	else
		return 0;	
}


/*******************************************/
/* Data-sharing attribute */
/*******************************************/
/**
 * ---------------------------------------------------------------------
 * Set data attribute: default(none)
 * ---------------------------------------------------------------------
 */
void set_default_none(VarList *vlist_tail, char level){
	VarList *vtail;
	vtail = vlist_tail;
	while ((vtail != NULL ) && (vtail->var.level == level) ){
		vtail->var.pro = CAPE_PRIVATE;		
		vtail = vtail->prev;
	}
}

/**
 * ---------------------------------------------------------------------
 * Set data attribute
 * ---------------------------------------------------------------------
 */
void set_data_attribute(VarList *vlist_tail, long addr, char pro, char level){
	VarList *vtail;
	vtail = vlist_tail;
	while ((vtail != NULL ) && (vtail->var.level == level) ){
		if (vtail->var.addr == addr) {
			vtail->var.pro = pro;
			break;		
		}
		vtail = vtail->prev;
	}
}

/**
 * ---------------------------------------------------------------------
 * Set thread private
 * ---------------------------------------------------------------------
 */
void set_threadprivate(VarList *vlist_head, long addr){
	VarList *vhead;
	vhead = vlist_head;
	while (vhead != NULL ){
		if (vhead->var.addr == addr) {
			vhead->var.pro = CAPE_PRIVATE;
			break;		
		}
		vhead = vhead->next;
	}
}


/***********************************************************************/
/*		Clauses and data sharing directives							   */
/***********************************************************************/
void cape_set_default_none(){
	set_default_none(__var_list_tail, __parallel_level__);
}
void cape_set_threadprivate(long addr){
	set_threadprivate(__var_list_head, addr);
}
void cape_set_shared(long addr){
	set_data_attribute(__var_list_tail, addr, CAPE_SHARED, __parallel_level__);
}
void cape_set_private(long addr){
	set_data_attribute(__var_list_tail, addr, CAPE_PRIVATE, __parallel_level__);
}
void cape_set_firstprivate(long addr){
	set_data_attribute(__var_list_tail, addr, CAPE_FIRST_PRIVATE, __parallel_level__);
}    
void cape_set_lastprivate(long addr){
	set_data_attribute(__var_list_tail, addr, CAPE_LAST_PRIVATE, __parallel_level__);
}
void cape_set_reduction(long addr, char op){
	set_data_attribute(__var_list_tail, addr, op , __parallel_level__);
}
void cape_set_copyin(long addr){
	set_data_attribute(__var_list_tail, addr, CAPE_COPY_IN, __parallel_level__);
}
void cape_set_copythread(long addr){
	set_data_attribute(__var_list_tail, addr, CAPE_COPY_PRIVATE, __parallel_level__);
}





/*****************************************/
void print_var_list(VarList *vlist)
{
	printf("PRINT ACTIVE VARIABLE LIST: \n");
	if(vlist != NULL){
		while(vlist!=NULL){
			printf("Node %ld: Variables: Ox%lx - size: %u - n: %u- pro: %d - dtype: %d - level: %d - is pointer: %d \n ", __node__, 	 \
					vlist->var.addr, 
					vlist->var.size, 
					vlist->var.n, 
					vlist->var.pro, 
					vlist->var.dtype,
					vlist->var.level,
					vlist->var.ispointer );
			vlist = vlist->next;
		}
	}
	else
		printf("VarList is NULL \n");
}

void print_var_head_list(PointerList *vlist){
	if(vlist != NULL){
		while(vlist!=NULL){
			printf("\n Node %d: In Heap: Manager addr: Ox%lx - Addr: 0x%lx - %ld bytes \n ", __node__, 	 \
					vlist->pointer.manager_addr, vlist->pointer.addr, vlist->pointer.len);
			vlist = vlist->next;
		}
	}
	else
		printf("VarList is NULL \n");
}



int print_data_in_checkpoint(char *data_ckpt, size_t file_size){
	unsigned long len=0, file_pointer = 0;
	unsigned long addr, pc, size_s, timestamp, i;
	
	timestamp = *(unsigned long *) data_ckpt ;
	pc = *(unsigned long *)(data_ckpt+ 4); //get program counter
	size_s =  *(unsigned long *)(data_ckpt+ 8);
	
	file_pointer = 12 ;
	
	printf("\nNode: %ld - timestamp: %ld - PC: Ox%lx - Size_S: %ld \n", __node__, timestamp, pc, size_s );
	
	while(file_pointer < file_size){
		
		addr = *(long *) (data_ckpt + file_pointer);		
		file_pointer += sizeof(long);		
		
		len = *(unsigned int*) (data_ckpt + file_pointer );
		file_pointer += sizeof(long);			
		
		printf(" ------ Node: %ld - Addr: 0x%lx - Len: %ld - Data: ", __node__, addr, len );
		for(i = 0; i < len/4 ; i++ ){
			printf("\t%d", *(int *) (data_ckpt + file_pointer));
			file_pointer += sizeof(int);
		}
		
		
		//if (__node__ == 0)
		//	printf("DATA: Node %d: addr = 0x%lx - len = %d bytes - data %d \n",__node__, addr, len, *(int *) (data_ckpt+ file_pointer) );	
				
		//file_pointer += len ;			
	}
	
	return 1;
}


/****************************************************************************************
 * public functions
 * **************************************************************************************
 */

/*
 * cape_declare_variable: version 7.0
 * 	Save all active and shared variables in __active_variable list
 *  (global and local variable is declared outside #pragma omp parallel)
 */
int cape_declare_variable(unsigned long addr,
						  unsigned char dtype,
						  unsigned int n_elements,
						  unsigned char ispointer){
							  
	if (__is_inside_parallel_region__) return 0;
	
	Var v;
	v.addr = addr;
	v.dtype = dtype;
	v.n = n_elements;
	v.pro = CAPE_SHARED;
	v.level = __activate_func_level__ ;
	v.ispointer = ispointer;
	unsigned int size;
	if (ispointer){
		size = 4 ;
	}
	else
	{
		switch (dtype){
			case CAPE_CHAR:
			case CAPE_UNSIGNED_CHAR:
				size = 1;
				break;
			case CAPE_INT:
			case CAPE_UNSIGNED_INT:
			case CAPE_LONG:
			case CAPE_UNSIGNED_LONG:
			case CAPE_FLOAT:
				size = 4 ;
				break;
			case CAPE_DOUBLE:
				size = 8 ;
				break;
			default:
				size = 4;
		}
	}
	v.size = size;
	int val;
	val = add_active_variable(&__active_variable_head, &__active_variable_tail, v);	
	return val;
}

/*
 * ---------------------------------------------------------------------
 * Declare a initialization of heap memory
 * v = cape_malloc(size)
 * v = cape_calloc(v, p, size);
 * ---------------------------------------------------------------------
 */
void cape_allocate_memory(unsigned long manager_addr, 
						unsigned long addr, unsigned long nbytes){
	Pointer pt ;	
	pt.manager_addr = manager_addr ;
	pt.addr = addr ;
	pt.len = nbytes;	
	remove_exists_item(&__var_heap_list_head, &__var_heap_list_tail, pt);
	addnew_pointer(&__var_heap_list_head, &__var_heap_list_tail, pt) ;
		
} 
 /*
  *  ---------------------------------------------------------------------
  * Re-allocate heap memory
  * v = cape_reaalloc(p, size)
  * ---------------------------------------------------------------------
  */
void cape_reallocate_memory(unsigned long manager_addr, unsigned long old_addr,
						unsigned long addr, unsigned long nbytes){
	Pointer pt, pt_new ;	
	pt.manager_addr = manager_addr ;
	pt.addr = old_addr ;
	pt.len = 4;	
	remove_exists_item(&__var_heap_list_head, &__var_heap_list_tail, pt);
	
	pt_new.manager_addr = manager_addr;
	pt_new.addr = addr ;
	pt_new.len = nbytes ;
	addnew_pointer(&__var_heap_list_head, &__var_heap_list_tail, pt) ;
		
} 

/*
 * ---------------------------------------------------------------------
 * free(v);
 * ---------------------------------------------------------------------
 */
void cape_free_memory(unsigned long manager_addr){
	remove_heap_variables(&__var_heap_list_head,
						&__var_heap_list_tail,
						manager_addr);	
} 

/*
 * ---------------------------------------------------------------------
 * TODO: Initialize 
 * Version 7.0
 * ---------------------------------------------------------------------
 */
void cape_init(){	
	MPI_Init(NULL,NULL);
	MPI_Comm_rank(MPI_COMM_WORLD, &__node__);
	MPI_Comm_size(MPI_COMM_WORLD, &__nnodes__);	
	
}
/*
 * ---------------------------------------------------------------------
 * TODO: Release the initialization envrironment
 * ---------------------------------------------------------------------
 */
void cape_finalize(){
	MPI_Finalize();		
}
/*
 * ---------------------------------------------------------------------
 * Setup variables environments for a block
 * ---------------------------------------------------------------------
 */
void cape_begin(unsigned char name_directive, long long first, long long second){
	switch(name_directive){
		case PARALLEL:
			open_parallel_window();
			break;
		case FOR:
			//ckpt_stop();
			__parallel_level__++;
			add_parallel_region(&__var_list_head, &__var_list_tail, __parallel_level__);
			__left__ = cape_get_left(second, first);
			__right__= cape_get_right(second, first);
			break;
		case FOR_NOWAIT:
			__left__ = cape_get_left(second, first);
			__right__= cape_get_right(second, first);
			break;
		case PARALLEL_FOR:
			open_parallel_window();
			__left__ = cape_get_left(second, first);
			__right__= cape_get_right(second, first);			
			break;
		case CRISTIAL:
			break;
		case SECTIONS_NOWAIT:
			__current_session__ = -1 ;
			break;
		case SECTIONS:
			__parallel_level__++;
			add_parallel_region(&__var_list_head, &__var_list_tail, __parallel_level__);
			__current_session__ = -1 ;
			break;
		default:
			break;
	}	
}
/*
 * ---------------------------------------------------------------------
 * Release variables environments of a block
 * ---------------------------------------------------------------------
 */
void cape_end(unsigned char name_directive, unsigned char ops_flag){	
	switch(name_directive){
		case FOR:
			__time_stamp__ = __right__;			
			require_generate_checkpoint(ops_flag);
			ckpt_stop();
			require_allreduce(EXIT_CHECKPOINT);			
			inject_checkpoint(after_ckpt, after_ckpt_size);
			release_checkpoint();
			remove_parellel_region(__parallel_level__);
			__parallel_level__ --;
			ckpt_start();
			break;
		case FOR_NOWAIT:
			break;
		case SECTIONS_NOWAIT:
			break;		
		case PARALLEL:
			__time_stamp__ = __pc__;
			require_generate_checkpoint(ops_flag);
			ckpt_stop();
			require_allreduce(EXIT_CHECKPOINT);
			inject_checkpoint(after_ckpt, after_ckpt_size);
			release_checkpoint();
			close_parallel_window();
			break;			
		case PARALLEL_FOR:	
			__time_stamp__ = __right__;
			require_generate_checkpoint(ops_flag);
			ckpt_stop();
			require_allreduce(EXIT_CHECKPOINT);	
			//print_data_in_checkpoint(after_ckpt, after_ckpt_size);			
			inject_checkpoint(after_ckpt, after_ckpt_size);
			release_checkpoint();
			close_parallel_window();			
			break;
		case SECTIONS:
			__time_stamp__ = __pc__;
			require_generate_checkpoint(ops_flag);
			ckpt_stop();
			require_allreduce(EXIT_CHECKPOINT);			
			inject_checkpoint(after_ckpt, after_ckpt_size);
			release_checkpoint();
			remove_parellel_region(__parallel_level__);
			__parallel_level__ --;
			ckpt_start();
			break;
		default:
			break;
	}		
}
/*
 * --------------------------------------------------------------------
 * Copy data into ckpt_data in FILE in memory 
 * Structure of data {(addr, data), ....}
 * ---------------------------------------------------------------------
 */
int ckpt_start(){	
	//openfile
	ckpt_data_stream = open_memstream(&ckpt_data, &ckpt_data_size);		
	int size;		
	if(__var_list_head == NULL) return 0;
		
	VarList *tmp;
	tmp = __var_list_tail;
	
	//move to the first variable of current parallel level
	while((tmp->var.level == __parallel_level__) && (tmp->prev != NULL))
		tmp = tmp->prev ;
	
	while(tmp != NULL){
		if ((tmp->var.pro != CAPE_PRIVATE) &&
			(tmp->var.pro != CAPE_THREAD_PRIVATE)){
		
			fwrite(&tmp->var.addr, sizeof(unsigned long), 1, ckpt_data_stream);
			fwrite(tmp->var.addr, tmp->var.size, tmp->var.n, ckpt_data_stream);
			fflush(ckpt_data_stream);	
			// printf("Write to ckpt data file: Ox%lx - pro: %d  - size : %d \n", tmp->var.addr, tmp->var.pro, \
										sizeof(unsigned long)  + tmp->var.size * tmp->var.n);
		}
		
		tmp = tmp->next;
	}
	
	//printf("Size of CKPT_DATA_FILE: %ld", ckpt_data_size);
	
}
/*
 * --------------------------------------------------------------------
 * Copy data into ckpt_data in FILE in disk 
 * Structure of data {(addr, data), ....}
 * ---------------------------------------------------------------------
 */
int ckpt_start_2(){
	if (__node__< 0) __node__ = 1;
	sprintf(__ckpt_data_file, "ckpt_data%d.tmp", __node__);	
	__ckpt_data = fopen(__ckpt_data_file, "wb+");
	__ckpt_data_size == 0;
		
	int size;
		
	if(__var_list_head == NULL) return 0;
	
	VarList *tmp;
	tmp = __var_list_tail;
	
	//move to the first variable of current parallel level
	while((tmp->var.level == __parallel_level__) && (tmp->prev != NULL))
		tmp = tmp->prev ;
	
	while(tmp != NULL){
		if ((tmp->var.pro != CAPE_PRIVATE) &&
			(tmp->var.pro != CAPE_THREAD_PRIVATE)){			
			fwrite(&tmp->var.addr, sizeof(unsigned long), 1, __ckpt_data);
			size = tmp->var.n * tmp->var.size ;			
			fwrite(tmp->var.addr, tmp->var.size, tmp->var.n, __ckpt_data);
			fflush(__ckpt_data);	
			__ckpt_data_size += sizeof(unsigned long) \							
								+ tmp->var.size * tmp->var.n ; 
			
			printf("Write to ckpt data file: Ox%lx - pro: %d  - size : %d \n", tmp->var.addr, tmp->var.pro, 
										sizeof(unsigned long)  + tmp->var.size * tmp->var.n);
		}
		
		tmp = tmp->next;
	}
	
	printf("Size of CKPT_DATA_FILE: %ld", __ckpt_data_size);
	
}

/*
 * ---------------------------------------------------------------------
 * TODO: close __ckpt_data file and syncronization data
 * ---------------------------------------------------------------------
 */
void ckpt_stop(){
	if (ckpt_data_size > 0){
		fclose(ckpt_data_stream);
		free(ckpt_data);
		ckpt_data_size = 0;	
	}
}

void ckpt_stop_2(){
	fclose(__ckpt_data);
}

/*
 * ---------------------------------------------------------------------
 * Set flush()
 * ---------------------------------------------------------------------
 */
void cape_flush(){		
	require_generate_checkpoint(FALSE);
	ckpt_stop();
	require_allreduce(EXIT_CHECKPOINT);			
	inject_checkpoint(after_ckpt, after_ckpt_size);
	release_checkpoint();
	ckpt_start();
}
/*
 * ---------------------------------------------------------------------
 * Set barrier
 * ---------------------------------------------------------------------
 */
void cape_barrier(){
	__time_stamp__  = __node__;
	cape_flush();
}
/* --------------------*/

void CAPE_DEBUG()
{	
	print_var_list(__active_variable_head);	
	printf("\n-----------\n");
	//print_var_list(__active_variable_head);	
	print_var_head_list(__var_heap_list_head);
	
	
}

/*
 * -------------------------------------------------------------------
 * Enter function: v7.0
 * 	Setup function variable of function in activation tree *  
 * --------------------------------------------------------------------
 */
void __enter_func(){
	__activate_func_level__ ++;
}
/*
 * -------------------------------------------------------------------
 * Exit function: v7.0
 * 	Remove parameters and local variables from shared variable list *  
 * --------------------------------------------------------------------
 */
void __exit_func(){
	remove_active_variables(&__active_variable_head,
							&__active_variable_tail,
							__activate_func_level__);	
	__activate_func_level__ --;
}

void require_generate_checkpoint(char ops_flag){
			
	after_ckpt_stream = generate_checkpoint(__var_list_head,
								__parallel_level__,		\
								EXIT_CHECKPOINT,				\
								ops_flag,						\
								__time_stamp__,		\ 
								__pc__);	
	
}
