#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <dlfcn.h>

struct user_pt_regs {
    __u64 regs[31];
    __u64 sp;
    __u64 pc;
    __u64 pstate;
};

uint64_t get_module_base(pid_t pid, const char* module_name) {
    char filename[64];
    snprintf(filename, sizeof(filename), "/proc/%d/maps", pid);
    FILE* fp = fopen(filename, "r");
    if (!fp) return 0;
    char line[512];
    uint64_t base_addr = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, module_name) && strstr(line, "r-xp")) {
            sscanf(line, "%lx-%*lx %*s %*lx %*s %*s", &base_addr);
            break;
        }
    }
    fclose(fp);
    return base_addr;
}

uint64_t remote_call(pid_t pid, uint64_t func_addr, uint64_t* args, int arg_count) {
    struct user_pt_regs regs;
    struct iovec io = { .iov_base = &regs, .iov_len = sizeof(regs) };
    if (ptrace(PTRACE_GETREGSET, pid, NT_PRSTATUS, &io) == -1) return 0;
    struct user_pt_regs saved_regs = regs;
    for (int i = 0; i < arg_count && i < 8; i++) regs.regs[i] = args[i];
    regs.pc = func_addr;
    if (ptrace(PTRACE_SETREGSET, pid, NT_PRSTATUS, &io) == -1) return 0;
    ptrace(PTRACE_CONT, pid, 0, 0);
    int status;
    waitpid(pid, &status, 0);
    ptrace(PTRACE_GETREGSET, pid, NT_PRSTATUS, &io);
    uint64_t ret_val = regs.regs[0];
    struct iovec io_saved = { .iov_base = &saved_regs, .iov_len = sizeof(saved_regs) };
    ptrace(PTRACE_SETREGSET, pid, NT_PRSTATUS, &io_saved);
    return ret_val;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_oxidelauncher_MainActivity_injectLibrary(JNIEnv *env, jobject, jint pid, jstring lib_path) {
    const char* path = env->GetStringUTFChars(lib_path, 0);
    if (ptrace(PTRACE_ATTACH, pid, 0, 0) == -1) {
        env->ReleaseStringUTFChars(lib_path, path);
        return JNI_FALSE;
    }
    int status;
    waitpid(pid, &status, 0);
    uint64_t target_libc_base = get_module_base(pid, "libc.so");
    if (!target_libc_base) {
        ptrace(PTRACE_DETACH, pid, 0, 0);
        env->ReleaseStringUTFChars(lib_path, path);
        return JNI_FALSE;
    }
    void* local_libc = dlopen("libc.so", RTLD_LAZY);
    uint64_t local_mmap = (uint64_t)dlsym(local_libc, "mmap");
    uint64_t local_dlopen = (uint64_t)dlsym(local_libc, "dlopen");
    uint64_t local_libc_base = get_module_base(getpid(), "libc.so");
    uint64_t target_mmap = target_libc_base + (local_mmap - local_libc_base);
    uint64_t target_dlopen = target_libc_base + (local_dlopen - local_libc_base);
    uint64_t mmap_args[] = {0, 256, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, (uint64_t)-1, 0};
    uint64_t remote_mem = remote_call(pid, target_mmap, mmap_args, 6);
    struct iovec local_iov = { .iov_base = (void*)path, .iov_len = strlen(path) + 1 };
    struct iovec remote_iov = { .iov_base = (void*)remote_mem, .iov_len = strlen(path) + 1 };
    process_vm_writev(pid, &local_iov, 1, &remote_iov, 1, 0);
    uint64_t dlopen_args[] = {remote_mem, RTLD_NOW};
    uint64_t handle = remote_call(pid, target_dlopen, dlopen_args, 2);
    ptrace(PTRACE_DETACH, pid, 0, 0);
    env->ReleaseStringUTFChars(lib_path, path);
    return handle != 0 ? JNI_TRUE : JNI_FALSE;
}
