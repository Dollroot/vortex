#include <iostream>
#include <unistd.h>
#include <unistd.h>
#include <string.h>
#include <vortex.h>
#include "common.h"

const char* program_file = nullptr;

static void show_usage() {
   std::cout << "Vortex Driver Test." << std::endl;
   std::cout << "Usage: -f: program [-h: help]" << std::endl;
}

static void parse_args(int argc, char **argv) {
  int c;
  while ((c = getopt(argc, argv, "f:h?")) != -1) {
    switch (c) {
    case 'f': {
      program_file = optarg;
    } break;
    case 'h':
    case '?': {
      show_usage();
      exit(0);
    } break;
    default:
      show_usage();
      exit(-1);
    }
  }

  if (nullptr == program_file) {
    show_usage();
    exit(-1);
  }
}

vx_device_h device;
vx_buffer_h buffer;

void cleanup() {
  if (device) {
    vx_dev_close(device);
  }
  if (buffer) {
    vx_buf_release(buffer);
  }
}

int main(int argc, char *argv[]) {
  int ret;
  int errors = 0;
  size_t value; 
  kernel_arg_t kernel_arg;

  uint32_t stride = BLOCK_SIZE / sizeof(uint32_t);
  uint32_t num_points = MAX_CORES * MAX_WARPS * MAX_THREADS * stride;
  uint32_t buf_size = num_points * sizeof(uint32_t);
  
  // parse command arguments
  parse_args(argc, argv);

  // open device connection
  std::cout << "open device connection" << std::endl;  
  ret = vx_dev_open(&device);
  if (ret != 0)
    return -1;

  // upload program
  std::cout << "upload program" << std::endl;  
  ret = vx_upload_kernel_file(device, program_file);
  if (ret != 0) {
    cleanup();
    return -1;  
  }

  // allocate device memory
  std::cout << "allocate device memory" << std::endl;  

  ret = vx_alloc_dev_mem(device, buf_size, &value);
  if (ret != 0) {
    cleanup();
    return -1;  
  }
  kernel_arg.src0_ptr = value;

  ret = vx_alloc_dev_mem(device, buf_size, &value);
  if (ret != 0) {
    cleanup();
    return -1;  
  }
  kernel_arg.src1_ptr = value;

  ret = vx_alloc_dev_mem(device, buf_size, &value);
  if (ret != 0) {
    cleanup();
    return -1;  
  }
  kernel_arg.dst_ptr = value;

  // allocate shared memory  
  std::cout << "allocate shared memory" << std::endl;    
  ret = vx_alloc_shared_mem(device, buf_size, &buffer);
  if (ret != 0) {
    cleanup();
    return -1;  
  }

  // populate source buffer values
  std::cout << "populate source buffer values" << std::endl;    
  {
    auto buf_ptr = (int*)vx_host_ptr(buffer);
    for (uint32_t i = 0; i < num_points; ++i) {
      buf_ptr[i] = i;
    }
  }

  // upload source buffers
  std::cout << "upload source buffers" << std::endl;    
  
  ret = vx_copy_to_dev(buffer, kernel_arg.src0_ptr, buf_size, 0);
  if (ret != 0) {
    cleanup();
    return -1;  
  }

  ret = vx_copy_to_dev(buffer, kernel_arg.src1_ptr, buf_size, 0);
  if (ret != 0) {
    cleanup();
    return -1;  
  }

  // upload kernel argument
  std::cout << "upload kernel argument" << std::endl;
  {
    kernel_arg.stride = stride;

    auto buf_ptr = (int*)vx_host_ptr(buffer);
    memcpy(buf_ptr, &kernel_arg, sizeof(kernel_arg_t));
    ret = vx_copy_to_dev(buffer, KERNEL_ARG_DEV_MEM_ADDR, sizeof(kernel_arg_t), 0);
    if (ret != 0) {
      cleanup();
      return -1;  
    }
  }

  // start device
  std::cout << "start device" << std::endl;
  ret = vx_start(device);
  if (ret != 0) {
    cleanup();
    return -1;  
  }

  // wait for completion
  std::cout << "wait for completion" << std::endl;
  ret = vx_ready_wait(device, -1);
  if (ret != 0) {
    cleanup();
    return -1;  
  }

  // download destination buffer
  std::cout << "download destination buffer" << std::endl;
  ret = vx_copy_from_dev(buffer, kernel_arg.dst_ptr, buf_size, 0);
  if (ret != 0) {
    cleanup();
    return -1;  
  }

  // verify result
  std::cout << "verify result" << std::endl;  
  {
    auto buf_ptr = (int*)vx_host_ptr(buffer);
    for (uint32_t i = 0; i < num_points; ++i) {
      int ref = i * i; 
      int cur = buf_ptr[i];
      if (cur != ref) {
        ++errors;
      }
    }
  }

  // cleanup
  std::cout << "cleanup" << std::endl;  
  cleanup();

  if (0 == errors) {
    printf("PASSED!\n");
  } else {
    printf("FAILED!\n");
  }

  return errors;
}