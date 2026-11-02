#include "rstack.h"
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <ctype.h>



static uint64_t dfs_counter = 0;
// bottom stack will have the connection on arr
// top stack will have the connection in parrent_list
// if stack have been freed but the connection haven't been
// freed yet, a pointer to it will be null
// first node of a list works differently,
// rstack pointers it has are null
struct connection{
	struct connection *prev_connection, *next_connection;
	rstack_t *bottom_stack, *top_stack;
};
typedef struct connection rstack_connection;

typedef struct{
	enum {INT, STACK} type;
	union{
		uint64_t value;
		rstack_connection* ptr;
	};
} rstack_member;

struct rstack{
	size_t size, range;
	rstack_member *arr;
	// first node of the list will be {null, ptr, null, null}, where ptr
	// is a pointer to second node
	// it's static so it won't be removed
	rstack_connection parents_list; 
	bool deleted;
	// for dfs purposes
	uint64_t last_dfs;
};

rstack_t* rstack_new(){
	rstack_t* out = (rstack_t*)malloc(sizeof(rstack_t));
	if (out == nullptr){
		errno = ENOMEM;
		return nullptr;
	}
	out->size = 0;
	out->range = 1;
	out->arr = (rstack_member*)malloc(sizeof(rstack_member));
	if (out->arr == nullptr){
		errno = ENOMEM;
		free(out);
		return nullptr;
	}
	out->parents_list = 
		(rstack_connection){nullptr, nullptr, nullptr, nullptr};
	out->deleted = false;
	out->last_dfs = 0;
	return out;
}

void new_simple_dfs(){dfs_counter++;}
// for dfs that care about cycles
bool dfs_is_visited(rstack_t* rs){return rs->last_dfs == dfs_counter;}
void dfs_visit(rstack_t* rs){rs->last_dfs = dfs_counter;}

void new_enter_exit_dfs(){dfs_counter += 2;}
// dfs that mark not_removable, to_be_listed, listed
// listed mean belong to remove_list
bool dfs_is_entered(rstack_t* rs){return rs->last_dfs == dfs_counter -1;}
bool dfs_is_exited(rstack_t* rs){return rs->last_dfs == dfs_counter;}
void dfs_enter(rstack_t* rs){rs->last_dfs = dfs_counter -1;}
void dfs_exit(rstack_t* rs){rs->last_dfs = dfs_counter;}

void new_remove_dfs(){dfs_counter += 4;}
bool dfs_is_not_removable(rstack_t* rs){return rs->last_dfs == dfs_counter -3;}
bool dfs_is_to_be_listed(rstack_t* rs){return rs->last_dfs == dfs_counter -2;}
bool dfs_is_listed(rstack_t* rs){return rs->last_dfs == dfs_counter -1;}
void dfs_not_removable(rstack_t* rs){rs->last_dfs = dfs_counter -3;}
void dfs_to_be_listed(rstack_t* rs){rs->last_dfs = dfs_counter -2;}
void dfs_listed(rstack_t* rs){rs->last_dfs = dfs_counter -1;}

rstack_connection* new_connection(rstack_t* rs1, rstack_t* rs2){
	// assumes rs2 is not nullptr, rs1 may be null
	// return nullptr if memory allocation error occured
	// and set's errno
	
	rstack_connection* connection = 
		(rstack_connection*)malloc(sizeof(rstack_connection));
	if (connection == nullptr){
		errno = ENOMEM;
		return nullptr;
	}

	// the connection that will stay in list right after 
	// output - the new connection
	rstack_connection* following_connection = 
		rs2->parents_list.next_connection;
	
	*connection = (rstack_connection){
		.prev_connection = &rs2->parents_list,
		.next_connection = following_connection,
		.bottom_stack = rs1,
		.top_stack = rs2
	};
	if (following_connection)
		following_connection->prev_connection = connection;
	rs2->parents_list.next_connection = connection;
	return connection;
}

void connection_take_off_list(rstack_connection* c){
	// it will be used when poping top of stack, when it's a stack,
	// so a connection no longer exist
	// keeps parents_lists it belongs intact
	// the function may be called after freeing stacks c is connecting
	// it
	if (c == nullptr) return;

	if (c->prev_connection) 
		c->prev_connection->next_connection = c->next_connection;
	if (c->next_connection) 
		c->next_connection->prev_connection = c->prev_connection;
	c->prev_connection = nullptr;
	c->next_connection = nullptr;
}

bool can_be_removed_dfs(rstack_t *rs){
	if (!(rs->deleted)) return 0;

	if (dfs_is_visited(rs) || dfs_is_listed(rs)) return 1;
	dfs_visit(rs);

	rstack_connection *c = &rs->parents_list;
	while ((c = c->next_connection)){
		if (!can_be_removed_dfs(c->bottom_stack)) return 0;
	}
	return 1;
}

// dfs_change_state must change dfs_state to other than visited
void change_visited_to(rstack_t *rs, void (*dfs_change_state)(rstack_t*)){
	if (!dfs_is_visited(rs)) return;
	dfs_change_state(rs);

	rstack_connection *c = &rs->parents_list;
	while ((c = c->next_connection)){
		change_visited_to(c->bottom_stack, *dfs_change_state);
	}
}

void make_removed_list(rstack_t *rs, rstack_connection* removed_list){
	if (!dfs_is_to_be_listed(rs)) return;
	dfs_listed(rs);

	// can be removed, add to list
	// rs->parents_list.next_connection == nullptr can happend only
	// in special case of rs just having been deleted or poped out
	rstack_connection *c = rs->parents_list.next_connection;
	if (c){
		connection_take_off_list(c);
		// bottom_stack still has c but now it knows not to free it
		c->bottom_stack = nullptr; 
		c->prev_connection = removed_list;
		c->next_connection = removed_list->next_connection;
		if (removed_list->next_connection)
			removed_list->next_connection->prev_connection = c;
		removed_list->next_connection = c;
	}
}

// if rs can be removed returns true and last_dfs of all connected
// removable stacks to dfs_counter
// dfs must be initiated
// if rs does not have parent it's not added
void find_removable(rstack_t *rs, rstack_connection* removed_list){
	if (dfs_is_listed(rs)) return;
	if(can_be_removed_dfs(rs)){
		change_visited_to(rs, dfs_to_be_listed);
		make_removed_list(rs, removed_list);
		for(rstack_member* mem = rs->arr; mem<rs->arr + rs->size; mem++){
			if (mem->type == STACK){
				rstack_connection* connection = mem->ptr;
				// if connection->bottom_stack is nullptr then it's part of removed list
				// don't touch it here
				if (connection->bottom_stack == rs){
					find_removable(connection->top_stack, removed_list);
				}
			}
		}
	}
	else{
		change_visited_to(rs, dfs_not_removable);
	}
}

void rstack_free(rstack_t* rs){
	for (rstack_member* mem = rs->arr; mem < rs->arr + rs->size; mem++){
		if (mem->type == STACK){
			rstack_connection* connection = mem->ptr;
			// if connection->bottom_stack is nullptr then it's part of removed list
			// don't touch it here
			if (connection->bottom_stack == rs){
				connection_take_off_list(connection);
				free(connection);
			}
		}
	}
	free(rs->arr);
	if (rs->parents_list.next_connection) rs->parents_list.next_connection->prev_connection = nullptr;
	free(rs);
}

void try_removing(rstack_t* rs){
	rstack_connection removed_list = 
		(rstack_connection){nullptr, nullptr, nullptr, nullptr};
	bool parentless = rs->parents_list.next_connection == nullptr;
	new_remove_dfs();
	find_removable(rs, &removed_list);
	if (parentless && rs->deleted){
		rstack_free(rs);
	}
	rstack_connection* list = &removed_list;
	while((list = list->next_connection)){
		rstack_free(list->top_stack);
	}
 	list = removed_list.next_connection;
	while(list){
		rstack_connection* newlist = list->next_connection;
		free(list);
		list = newlist;
	}
}

void rstack_delete(rstack_t *rs){
	if (rs == nullptr) return;
	rs->deleted = true;

	try_removing(rs);
}

void rstack_pop(rstack_t *rs){
	if (!rs || rs->size == 0) return;
	rstack_member top_mem = rs->arr[--rs->size]; // taking top member off the stack
	
	if (top_mem.type == STACK){
		// INT can be left alone, but STACK requires some more care
		rstack_connection *c = top_mem.ptr;
		rstack_t* rstack_poped = c->top_stack;
		connection_take_off_list(c);
		free(c);
		// bottom stack might have been the only connection of top stack to user
		try_removing(rstack_poped);
	}
}

int rstack_push_member(rstack_t *rs, rstack_member mem){
	// returns -1 if error, 0 otherwise
	if (rs->size == rs->range){
		// stack must grow
		rstack_member* new_arr = (
			rstack_member*)realloc(rs->arr,
			sizeof(rstack_member) * rs->range*2
		);
		if (new_arr == nullptr){
			errno = ENOMEM;
			return -1;
		}
		rs->arr = new_arr;
		rs->range *= 2;
	}

	rs->arr[rs->size++] = mem;
	return 0;
}

int rstack_push_value(rstack_t *rs, uint64_t value){
	if (rs==nullptr){
		errno = EINVAL;
		return -1;
	}
	rstack_member mem = {.type = INT, .value = value};
	return rstack_push_member(rs, mem);
}

int rstack_push_rstack(rstack_t *rs1, rstack_t *rs2){
	if (rs1==nullptr){
		errno = EINVAL;
		return -1;
	}
	if (rs2==nullptr){
		errno = EINVAL;
		return -1;
	}
	rstack_connection* connection = new_connection(rs1, rs2);
	// in case of error new_connection set's errno and returns nullptr
	if (connection == nullptr){
		return -1;
	}

	rstack_member mem = {.type = STACK, .ptr = connection};
	if (rstack_push_member(rs1, mem) == 0) return 0;
	else{
		connection_take_off_list(connection);
		free(connection);
		return -1;
	}
}

#define empty_result ((result_t){false, 0})

result_t front_int_dfs(rstack_t* rs){
	if (dfs_is_visited(rs)) return empty_result;
	dfs_visit(rs);

	for (rstack_member* mem = rs->arr + rs->size-1; mem >= rs->arr; mem--){
		if (mem->type == INT) return (result_t){true, mem->value};
		else{
			result_t child_result = front_int_dfs(mem->ptr->top_stack);
			if (child_result.flag) return child_result;
		}
	}	
	return empty_result;
}

result_t rstack_front(rstack_t* rs){
	if (rs == nullptr) return empty_result;
	new_simple_dfs();
	return front_int_dfs(rs);
}

bool rstack_empty(rstack_t *rs){
	return rstack_front(rs).flag == false;
}


rstack_t* rstack_read(char const *path){
	if (path == nullptr){
		errno = EINVAL;
		return nullptr;
	}
	rstack_t *out = rstack_new();
	if (out == nullptr) return nullptr;
	FILE *file_ptr;

	if ((file_ptr = fopen(path, "r"))){

		// uint64 can be 20 decimal digit long at most
		// leaving space ending whitespace
		const size_t buffer_size = 21; 
		char buffer[buffer_size];
		char *str_end = buffer;
		uint64_t value;

		// if I return in while and str don't set errno, 
		// it will mean the content of file is invalid
		

#define error_return() do{ \
		rstack_delete(out);  \
		fclose(file_ptr);    \
		return nullptr;      \
	} while (0)

		while(true){
			errno = 0;
			*str_end = fgetc(file_ptr);
			// special cases on begin
			if (str_end == buffer){
				if (*str_end == EOF) break;
				if (isspace(*str_end)) continue;
				if (*str_end == '+'){
					str_end++;
					continue;
				}
			}
			if (str_end == buffer +1){
				if ((*buffer == '0' || *buffer == '+')
					&& (*str_end >= '0' && *str_end <= '9')){
					*buffer = *str_end;
					continue;
				}
				if ((*str_end == EOF || isspace(*str_end)) && *buffer == '+'){
					errno = EINVAL;
					error_return();
				}
			}
			if (*str_end == EOF || isspace(*str_end)){
				value = strtoull(buffer, nullptr, 10);
				if (errno == ERANGE){
					error_return();
				}
				if (rstack_push_value(out, value) == -1){
					error_return();
				}

				if (*str_end == EOF){
					break;
				}
				// then it's whitespace
				str_end = buffer;
				continue;
			}
			if (*str_end >= '0' && *str_end <= '9'){
				//only white character and EOF can end buffer
				if (str_end == buffer + buffer_size - 1){
					errno = ERANGE;
					error_return();
				}
				str_end++;
				continue;
			}
			//only illegal characters are left
			errno = EINVAL;
			error_return();
		}
		if (fclose(file_ptr) != 0){
			rstack_delete(out);
			return nullptr;
		}
		return out;
	} else{
		rstack_delete(out);
		return nullptr;
	}
}

// it's and enter_exit dfs
// returns true if cycle found - dfs ended
bool rstack_write_dfs(FILE* file_ptr, rstack_t *rs){
	if (dfs_is_entered(rs)) return true;
	dfs_enter(rs);

	for (rstack_member* mem = rs->arr; mem < rs->arr + rs->size; mem++){
		if (mem->type == INT){
			fprintf(file_ptr, "%lu\n", mem->value);
		} else{
			if (rstack_write_dfs(file_ptr, mem->ptr->top_stack)) return true;
		}
	}	
	dfs_exit(rs);
	return false;
}

int rstack_write(char const *path, rstack_t *rs){
	if (path == nullptr || rs == nullptr){
		errno = EINVAL;
		return -1;
	}
	FILE *file_ptr;

	if ((file_ptr = fopen(path, "w"))){
		new_enter_exit_dfs();
		rstack_write_dfs(file_ptr, rs);
		if (fclose(file_ptr) != 0) return -1;
		return 0;
	}
	else{
		return -1;
	}
}

