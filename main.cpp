#include <cstdlib>
#include <cstdio>
#include <cstdint>
#define COROUTINES_IMPL
#include "coroutines.hpp"
/*
  simple test for the coroutines library

 */
coroutines mngr = {};

void print_routine(void){
  for(int i = 0; i < 10 + mngr.routines[mngr.routine_pointer].id ;i++){
	std::printf("Hello #%d from id %d\n",i,mngr.routines[mngr.routine_pointer].id);
	mngr.yeild();
  }
}

int main(){
  mngr = coroutines::new_manager();
  for(int i = 0;i < 5;i++){
	mngr.go(print_routine);
  }
  mngr.wait_till_finish();
  return 0;
}
