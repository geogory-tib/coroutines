#pragma once

#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <algorithm>

// assume 4k pages for now until mmap is used
#define PAGE_SIZE  4096



using routine_main_t = void(*)(void);

// only state needed is the instruction pointer and stack pointers
// the register state will be saved on the stack of the current routine

struct coroutine_t{
  std::uint64_t rip = 0;
  std::uint64_t rsp = 0;
  std::uint64_t rbp = 0;
  void *stack_page = nullptr;
  routine_main_t main = nullptr;
  std::uint64_t id = 0;
};

struct coroutines;


__attribute__((naked))void save_and_context_switch(coroutine_t *prev_routine,coroutine_t *new_routine);
__attribute__((naked)) void save_and_call_routine(coroutine_t *current_routine,void *new_routine_stack,coroutines *mngr);

int main();
struct coroutines{
  std::vector<coroutine_t>routines;
  std::vector<void *>free_stacks;   //stacks that have been freed from routines that have finished
  std::uint64_t routine_pointer = 0;
  std::uint64_t next_routine_id = 1;
  static coroutines new_manager(){   // creates the manager and makes the main routine;
	coroutines ret = {};
	coroutine_t main_routine = {};
	main_routine.main = reinterpret_cast<routine_main_t>(&main);
	main_routine.id = 0;
	ret.routines.push_back(main_routine);
	return ret;
  }
  void go(routine_main_t routine_main){
	coroutine_t new_routine = {};
	new_routine.main = routine_main;
	new_routine.id = next_routine_id;
	next_routine_id++;
	if(free_stacks.empty()){
	  // update this at some point to use MMAP or some other system call
	  new_routine.stack_page = malloc(PAGE_SIZE * 10);
	}else{
	  new_routine.stack_page = free_stacks.back();
	  free_stacks.pop_back();
	}
	new_routine.rbp = reinterpret_cast<std::uint64_t>(new_routine.stack_page) + (PAGE_SIZE * 10) - 1;
	routines.push_back(new_routine);
	int next_routine = routines.size() - 1;
	coroutine_t *current_routine = &routines[routine_pointer];
	routine_pointer = next_routine;
	save_and_call_routine(current_routine, (void *)new_routine.rbp,this);
  }
  void yeild(){
	coroutine_t *current_routine = &routines[routine_pointer];
	int next_routine = 0;
	next_routine = (routine_pointer + 1) % routines.size();
	if(current_routine->id == 0 && !free_stacks.empty()){
	  //when control get's passed back to the main routine it will free the used stacks one at a time
	  free(free_stacks.back());
	  free_stacks.pop_back();
	}
	if(routines.size() == 1){
	  return;
	}
	routine_pointer = next_routine;
	coroutine_t *new_routine = &routines[routine_pointer];
	save_and_context_switch(current_routine, new_routine);
  }
  // will halt execution of the main coroutine until
  // all routines have finished;
  void wait_till_finish(){
	while(routines.size() > 1){
	  yeild();
	}
  }
};
#ifdef COROUTINES_IMPL
__attribute__((naked))void save_and_context_switch(coroutine_t *prev_routine,coroutine_t *new_routine){
  __asm__ __volatile__(
					   ".intel_syntax noprefix\n\t"
					   "push rax\n\t"
					   "push rbx\n\t"
					   "push rdx\n\t"
					   "push rsi\n\t"
					   "push rdi\n\t"
					   "push r8\n\t"
					   "push r9\n\t"
					   "push r10\n\t"
					   "push r11\n\t"
					   "push r12\n\t"
					   "push r13\n\t"
					   "push r14\n\t"
					   "push r15\n\t"
					   "push 0\n\t" // two 0 pushes for stack alignment
					   "push 0 \n\t"
					   "lea rax,[rip + 1f]\n\t"
					   "mov qword ptr [rdi],rax\n\t"
					   "mov qword ptr [rdi + 8],rsp\n\t"
					   "mov qword ptr [rdi + 16],rbp\n\t"
					   "mov rsp,qword ptr[rsi + 8]\n\t"
					   "mov rbp,qword ptr[rsi + 16]\n\t"
					   "jmp [rsi]\n\t"
					   "1:\n\t"
					   "pop rax\n\t"
					   "pop rax\n\t"
					   "pop r15\n\t"
					   "pop r14\n\t"
					   "pop r13\n\t"
					   "pop r12\n\t"
					   "pop r11\n\t"
					   "pop r10\n\t"
					   "pop r9\n\t"
					   "pop r8\n\t"
					   "pop rdi\n\t"
					   "pop rsi\n\t"
					   "pop rdx\n\t"
					   "pop rbx\n\t"
					   "pop rax\n\t"
					   // "mov rsp,qword ptr [rdi + 8]\n\t"
					   // "mov rbp,qword ptr [rdi + 16]\n\t"
					   "ret\n\t"
					   ".att_syntax"
					  );
}
// called in the cleanup function to schedule new routine
__attribute__((naked))void context_switch_no_save(coroutine_t *new_routine){
  __asm__ __volatile__(
					   ".intel_syntax noprefix\n\t"
					   "mov rsp,qword ptr [rdi + 8]\n\t"
					   "mov rbp,qword ptr [rdi + 16]\n\t"
					   "jmp [rdi]\n\t"
					   ".att_syntax"
					  );

}
// calls the routine and will take care of cleanup when
// routine main returns
extern "C" void routine_caller(coroutines *mngr){
  coroutine_t *current_routine = &mngr->routines[mngr->routine_pointer];
  current_routine->main();
  mngr->free_stacks.push_back(current_routine->stack_page);
  mngr->routines.erase(mngr->routines.begin() + mngr->routine_pointer);
  mngr->routine_pointer = (mngr->routine_pointer + 1) % mngr->routines.size();
  coroutine_t *next_routine = &mngr->routines[mngr->routine_pointer];
  context_switch_no_save(next_routine);
}


__attribute__((naked)) void save_and_call_routine(coroutine_t *current_routine,void *new_routine_stack,coroutines *mngr){
  __asm__ __volatile__(
					   ".intel_syntax noprefix\n\t"
					   "push rax\n\t"
					   "push rbx\n\t"
					   "push rdx\n\t"
					   "push rsi\n\t"
					   "push rdi\n\t"
					   "push r8\n\t"
					   "push r9\n\t"
					   "push r10\n\t"
					   "push r11\n\t"
					   "push r12\n\t"
					   "push r13\n\t"
					   "push r14\n\t"
					   "push r15\n\t"
					   "push 0\n\t" // two 0 pushes for stack alignment
					   "push 0 \n\t"
					   "lea rax,[rip + 1f]\n\t"
					   "mov qword ptr [rdi],rax\n\t"
					   "mov qword ptr [rdi + 8],rsp\n\t"
					   "mov qword ptr [rdi + 16],rbp\n\t"
					   "mov rbp,rsi\n\t"
					   "mov rsp,rbp\n\t"
					   "mov rdi,rdx\n\t"
					   "call routine_caller\n\t"
					   "1:\n\t"
					   "pop rax\n\t"
					   "pop rax\n\t"
					   "pop r15\n\t"
					   "pop r14\n\t"
					   "pop r13\n\t"
					   "pop r12\n\t"
					   "pop r11\n\t"
					   "pop r10\n\t"
					   "pop r9\n\t"
					   "pop r8\n\t"
					   "pop rdi\n\t"
					   "pop rsi\n\t"
					   "pop rdx\n\t"
					   "pop rbx\n\t"
					   "pop rax\n\t"
					   "ret\n\t"
					   ".att_syntax"
					  );
}
#endif
