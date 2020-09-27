#include<stdio.h> 
#include<stdlib.h> 
#include "list.h"

int My402ListLength(My402List *dcll) {
	return dcll->num_members;
}

int My402ListEmpty(My402List *dcll) {
	if(0 == dcll->num_members) {
		return TRUE;
	} else {
		return FALSE;
	}
}

My402ListElem* createNewNode(void *obj) {
	My402ListElem* newNode = (My402ListElem*)malloc(sizeof(My402ListElem));
	if(NULL == newNode) {
		return NULL;
	}
	newNode->next = NULL;
	newNode->prev = NULL;
	newNode->obj = obj;
	return newNode;
}

My402ListElem *My402ListLast(My402List *dcll) {
	if(0 == dcll->num_members) {
		return NULL;
	} else {
		return dcll->anchor.prev;
	}
}

int My402ListAppend(My402List *dcll, void *obj) {
	My402ListElem* newNode = createNewNode(obj);
	if(NULL == newNode) {
		return FALSE;
	}
	if(0 == dcll->num_members) {
		dcll->anchor.next = newNode;
		dcll->anchor.prev = newNode;
		newNode->next = &(dcll->anchor);
		newNode->prev = &(dcll->anchor);
		dcll->num_members += 1;
		return TRUE;
	} else {
		My402ListElem* last = My402ListLast(dcll);
		last->next = newNode;
		newNode->prev = last;
		newNode->next = &(dcll->anchor);
		dcll->anchor.prev = newNode;
		dcll->num_members += 1;
		return TRUE;
	}
	return FALSE;
}

My402ListElem *My402ListFirst(My402List *dcll) {
	if(0 == dcll->num_members) {
		return NULL;
	} else {
		return dcll->anchor.next;
	}
}

int My402ListPrepend(My402List *dcll, void* obj) {
	My402ListElem* newNode = createNewNode(obj);
	if(0 == dcll->num_members) {
		dcll->anchor.next = newNode;
		dcll->anchor.prev = newNode;
		newNode->next = &(dcll->anchor);
		newNode->prev = &(dcll->anchor);
		dcll->num_members += 1;
		return TRUE;
	} else {
		My402ListElem* first = My402ListFirst(dcll);
		first->prev = newNode;
		newNode->next = first;
		newNode->prev = &(dcll->anchor);
		dcll->anchor.next = newNode;
		dcll->num_members += 1;
		return TRUE;
	}
	return FALSE;
}

My402ListElem *My402ListNext(My402List *dcll, My402ListElem *elem) {
	if(elem->next != &(dcll->anchor)) {
		return elem->next;
	} else {
		return NULL;
	}
}

My402ListElem *My402ListPrev(My402List *dcll, My402ListElem *elem) {
	if(elem->prev != &(dcll->anchor)) {
		return elem->prev;
	} else {
		return NULL;
	}
}

void My402ListUnlink(My402List *dcll, My402ListElem *elem) {
	if(0 != dcll->num_members) {
		(elem->next)->prev = elem->prev;
		(elem->prev)->next = elem->next;
		
		elem->prev = NULL;
		elem->next = NULL;
		elem->obj = NULL;

		free(elem);
		dcll->num_members -= 1;
	}
}

void My402ListUnlinkAll(My402List *dcll) {
	if(0 != dcll->num_members) {
		My402ListElem *node = dcll->anchor.next;
		for(My402ListElem *iter = node->next; iter != &(dcll->anchor); iter=node->next) {
			(node->prev)->next = node->next;
			(node->next)->prev = node->prev;
			node->next = NULL;
			node->prev = NULL;
			node->obj = NULL;
			free(node);
			node=iter;
		}
	}
}

int My402ListInsertBefore(My402List *dcll, void *obj, My402ListElem *elem) {
	if(NULL == elem) {
		if(TRUE == My402ListPrepend(dcll, obj)) {
			return TRUE;
		} else {
			return FALSE;
		}
	} else {
		My402ListElem *node = createNewNode(obj);
		if(NULL == node) {
			return FALSE;
		}
		node->next = elem;
		(elem->prev)->next = node;
		node->prev = elem->prev;
		elem->prev = node;
		dcll->num_members += 1;
		return TRUE;
	}
}

int My402ListInsertAfter(My402List *dcll, void *obj, My402ListElem *elem) {
	if(NULL == elem) {
		if(TRUE == My402ListAppend(dcll, obj)) {
			return TRUE;
		} else {
			return FALSE;
		}
	} else {
		My402ListElem *node = createNewNode(obj);
		if(NULL == node) {
			return FALSE;
		}
		node->next = elem->next;
		(elem->next)->prev = node;
		node->prev = elem;
		elem->next = node;
		dcll->num_members += 1;
		return TRUE;
	}
}

My402ListElem *My402ListFind(My402List *dcll, void *obj) {
	if(0 != dcll->num_members) {
		int localLength  = My402ListLength(dcll);
		//for(My402ListElem *node=dcll->anchor.next; node != &(dcll->anchor); node=node->next) {
		for(My402ListElem* node = My402ListFirst(dcll); localLength != 0; node = My402ListNext(dcll, node)) {
			if(node->obj == obj) {
				return node;
			}
			localLength -= 1;
		}
	}
	return NULL;
}

int My402ListInit(My402List* dcll) {
	dcll->num_members = 0;
	dcll->anchor.prev = &(dcll->anchor);
	dcll->anchor.next = &(dcll->anchor);
	dcll->anchor.obj = NULL;
	return TRUE;
}