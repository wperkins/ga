#include <iostream>   //for output
#include <stack>      //for stack
#include <vector>     //for vector
#include <functional> //for hash
#include <chrono>     //for time
#include <ctime>      //for time
#include <string.h>   //for string
#include <stdlib.h>
#include <unistd.h>

using namespace std;

struct GA_perf_thread {
   int tid;                       //thread id
   stack<int> entries;            //tracks order of function calls and nesting
   vector<int> function_times;    //(keys are indicies, values are the times)
   vector<string> function_names; //(function names link indicies to times)
};

struct GA_perf_node {
  int nid;                     //node id
  int nthreads;                //number of threads
  vector<GA_perf_thread> perf_threads; //reference to all thread structures
};

struct GA_perf_main {
  int nnodes;                 //number of nodes
  vector<GA_perf_node> perf_nodes; //reference to all node structures
};

//global
GA_perf_main perf_main;

GA_perf_node create_node(int nid)
{
	GA_perf_node perf_node;
	perf_node.nid = nid;
	return perf_node;
};

GA_perf_thread create_thread(int tid, GA_perf_node entry)
{
	GA_perf_thread perf_thread;
	perf_thread.tid = tid;
	
  	return perf_thread;
};


int get_func_hash_key(int nid, int tid, string hash_key_name)
{
  for (int i = 0; i < perf_main.perf_nodes[nid].perf_threads[tid].function_names.size(); i++)
    if (perf_main.perf_nodes[nid].perf_threads[tid].function_names[i] == hash_key_name)
      return i;
  return -1;
};


//seems to be COMPLETE
//adds an entry of when a function starting or ending
            //node id, thread id, tells what fuction it is, time metric, entry or exit
int add_entry(int nid, int tid, string func_hash_name, /*enum METRIC*/ int m, bool exit)
{
	//do node and thread exist?
	if (nid > perf_main.perf_nodes.size())
	  return -1;
	
	if (tid > perf_main.perf_nodes[nid].perf_threads.size())
      return -1;
    
	int func_hash_key = get_func_hash_key(nid, tid, func_hash_name); //placeholder
	
    //creates space for new function time to measure
	if (perf_main.perf_nodes[nid].perf_threads[tid].function_times.size() <= func_hash_key)
	{
		perf_main.perf_nodes[nid].perf_threads[tid].function_times.push_back(0);
		perf_main.perf_nodes[nid].perf_threads[tid].function_names.push_back(func_hash_name);
	}
	
	//return time if entry has already been tested...? does time need to be returned?	
	if (perf_main.perf_nodes[nid].perf_threads[tid].function_times[func_hash_key] != 0)
	  return perf_main.perf_nodes[nid].perf_threads[tid].function_times[func_hash_key];
	
	else if (exit == false)
    {
      unsigned long track_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now()).time_since_epoch()).count();
      //unsigned long track_time = std::clock();
      perf_main.perf_nodes[nid].perf_threads[tid].entries.push(track_time);
    }

    else if (exit == true)
	{
	  unsigned long current_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now()).time_since_epoch()).count();
      //unsigned long current_time = std::clock(); //std::chrono::high_resolution_clock::now();
      unsigned long prev_time = perf_main.perf_nodes[nid].perf_threads[tid].entries.top();
	  perf_main.perf_nodes[nid].perf_threads[tid].entries.pop();
	  unsigned long funct_time = current_time - prev_time;
	  perf_main.perf_nodes[nid].perf_threads[tid].function_times[func_hash_key] = funct_time;
	 
    }
	
	return 0;
	
};

//calculate function time for single thread
unsigned long accumulate_thread_value(int nid, int tid, /*enum METRIC*/ int m, string func_hash_name)
{
    int func_hash_key = get_func_hash_key(nid, tid, func_hash_name); 
    if (func_hash_key > -1)
	  return perf_main.perf_nodes[nid].perf_threads[tid].function_times[func_hash_key];
};

//calculate function time for node (all threads)
unsigned long accumulate_node_value(int nid, /*enum METRIC*/ int m, string func_hash_name)
{
    
  unsigned long total = 0;
  
  for (int i = 0; i < perf_main.perf_nodes[nid].perf_threads.size(); i++)
  {
    int func_hash_key = get_func_hash_key(nid, i, func_hash_name);
    if (func_hash_key > -1)
	  total += perf_main.perf_nodes[nid].perf_threads[i].function_times[func_hash_key];
  }
  return total;
};

//calculate function time for all nodes (and all threads)
unsigned long accumulate_total_value(/*enum METRIC*/ int m, string func_hash_name)
{
  unsigned long total = 0;

  for (int i = 0; i < perf_main.perf_nodes.size(); i++)
    for (int j = 0; j < perf_main.perf_nodes[i].perf_threads.size(); j++)
    {
        int func_hash_key = get_func_hash_key(i, j, func_hash_name);
        if (func_hash_key > -1)
	    total += perf_main.perf_nodes[i].perf_threads[j].function_times[func_hash_key];
    }
  
  return total;
};


//prints all values in the hash with times
void print_full_values()
{
  for (int i = 0; i < perf_main.perf_nodes.size(); i++)  
    for (int j = 0; j < perf_main.perf_nodes[i].perf_threads.size(); j++)
	  for (int k = 0; k < perf_main.perf_nodes[i].perf_threads[j].function_times.size(); k++)
	    printf("Node: %d, Thread: %d, Function_key: %d, Time: %d\n", i, j, k, perf_main.perf_nodes[i].perf_threads[j].function_times[k]);
};

//clears all previous node data
void clean_perf_structure()
{
  for (int i = 0; i < perf_main.perf_nodes.size(); i++)  
    for (int j = 0; j < perf_main.perf_nodes[i].perf_threads.size(); j++)
		perf_main.perf_nodes[i].perf_threads[j].function_times.clear();    //cleans the hash table to for all threads
};

/*************
* TEST CODE
**************/

//test measured functions
void functionA() {
   usleep(50000);
};

void functionB() {
   usleep(10000);
};

//basic test
int main(){

  int tid = 0; 
  int nid = 0; 
  int nnodes = 1; 
  int nthreads = 1;
  
  int i;
  int j;
  
  for (i = 0; i < nnodes; i++) {
	GA_perf_node nentry = create_node(i);
    perf_main.perf_nodes.push_back(nentry);
    for (j = 0 ; j < nthreads; j++) {
      GA_perf_thread tentry = create_thread(j, perf_main.perf_nodes[i]);
      perf_main.perf_nodes[i].perf_threads.push_back(tentry);
	}
  }
  
  add_entry(nid, tid, "functionA", 0, 0);
  functionA();
  add_entry(nid, tid, "functionA", 0, 1);
  
  add_entry(nid, tid, "functionB", 0, 0);
  functionB();
  add_entry(nid, tid, "functionB", 0, 1);
  
  unsigned long thread_time = accumulate_thread_value(nid,tid,0,"functionA");
  unsigned long node_time = accumulate_node_value(nid,0,"functionA");
  unsigned long total_time = accumulate_total_value(0,"functionA");
  
  thread_time = accumulate_thread_value(nid,tid,0,"functionB");
  node_time = accumulate_node_value(nid,0,"functionB");
  total_time = accumulate_total_value(0,"funcionB");
  
  print_full_values();

  clean_perf_structure();

}