#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mysql_connection.h>
#include <cppconn/driver.h>
#include <cppconn/connection.h>
#include <cppconn/exception.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>
#include <cppconn/prepared_statement.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/vfs.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>
#include <nvml.h>

typedef struct {
    char name_[20];
    unsigned long total_;
    char name2_[20];
    unsigned long free_;
} MEM_OCCUPY;

typedef struct {
    char name_[20];
    unsigned int user_;
    unsigned int nice_;
    unsigned int system_;
    unsigned int idle_;
} CPU_OCCUPY;

typedef struct {
    unsigned long long dt_;
    unsigned long long df_;
} DSK_OCCUPY;

static const int GPUNUM = 8;
typedef struct {
    int gpu[GPUNUM];
    int mem[GPUNUM];
} GPU_OCCUPY;

void get_memoccupy(MEM_OCCUPY* mem);
int cal_cpuoccupy(CPU_OCCUPY* o, CPU_OCCUPY* n);
void get_cpuoccupy(CPU_OCCUPY* cpust);
void get_dskoccupy(DSK_OCCUPY* dskst);
std::string getip();
nvmlReturn_t get_gpuoccupy(GPU_OCCUPY* g);

void update_db(const char* localip, int cpu, const MEM_OCCUPY& mem,
               const DSK_OCCUPY& dsk, const GPU_OCCUPY& gpu);

int main(int argc, char* argv[]) {
    CPU_OCCUPY cpu_stat1;
    CPU_OCCUPY cpu_stat2;
    MEM_OCCUPY mem_stat;
    DSK_OCCUPY dsk_stat;
    GPU_OCCUPY gpu_stat;

    unsigned long long dskinf_cnt = 0;
    int cpu = 0;
    while (1) {
        get_cpuoccupy(&cpu_stat1);
        sleep(1);
        get_cpuoccupy(&cpu_stat2);
        get_gpuoccupy(&gpu_stat);
        get_memoccupy(&mem_stat);
        cpu = cal_cpuoccupy(&cpu_stat1, &cpu_stat2);
        if (dskinf_cnt % 100 == 0) get_dskoccupy(&dsk_stat);
        update_db(argc > 1 ? argv[1] : getip().c_str(), cpu, mem_stat, dsk_stat,
                  gpu_stat);
        dskinf_cnt++;
    }
    return 0;
}

void get_memoccupy(MEM_OCCUPY* mem) {
    FILE* fp;
    char buff[256];
    MEM_OCCUPY* m;
    m = mem;
    fp = fopen("/proc/meminfo", "r");
    fgets(buff, sizeof(buff), fp);
    sscanf(buff, "%s %lu", m->name_, &m->total_);
    fgets(buff, sizeof(buff), fp);
    sscanf(buff, "%s %lu", m->name2_, &m->free_);
    fclose(fp);
}

int cal_cpuoccupy(CPU_OCCUPY* o, CPU_OCCUPY* n) {
    unsigned long od;
    unsigned long nd;
    unsigned long id;
    unsigned long sd;
    int cpu_use = 0;

    od = (unsigned long)(o->user_ + o->nice_ + o->system_ + o->idle_);
    nd = (unsigned long)(n->user_ + n->nice_ + n->system_ + n->idle_);
    id = (unsigned long)(n->user_ - o->user_);
    sd = (unsigned long)(n->system_ - o->system_);

    if ((nd - od) != 0)
        cpu_use = (int)((sd + id) * 100) / (nd - od);
    else
        cpu_use = 0;
    return cpu_use;
}

void get_cpuoccupy(CPU_OCCUPY* cpust) {
    FILE* fp;
    char buff[256];
    CPU_OCCUPY* cpu_occupy;
    cpu_occupy = cpust;
    fp = fopen("/proc/stat", "r");
    fgets(buff, sizeof(buff), fp);
    sscanf(buff, "%s %u %u %u %u", cpu_occupy->name_, &cpu_occupy->user_,
           &cpu_occupy->nice_, &cpu_occupy->system_, &cpu_occupy->idle_);
    fclose(fp);
}

void get_dskoccupy(DSK_OCCUPY* dskst) {
    /*
struct statfs diskinf;
statfs("/", &diskinf);
unsigned long long totalBlocks = diskinf.f_bsize;
unsigned long long totalSize = totalBlocks * diskinf.f_blocks;
unsigned long long freeSize = totalBlocks * diskinf.f_bavail;
dskst->dt_ = totalSize >> 20;
dskst->df_ = freeSize >> 20;
    */

    char buf[1024] = {0};
    char tmp1[1024] = {0};
    unsigned long long a = 0;
    unsigned long long b = 0;
    unsigned long long c = 0;
    unsigned long long dsktotal = 0;
    unsigned long long dskfree = 0;
    FILE* fp = popen("df -lP | grep /dev/", "r");
    if (fp) {
        while (fgets(buf, sizeof(buf), fp)) {
            sscanf(buf, "%s %llu %llu %llu", tmp1, &a, &b, &c);
            dsktotal += a;
            dskfree += c;
        }
        pclose(fp);
    }
    dskst->dt_ = dsktotal >> 10;
    dskst->df_ = dskfree >> 10;
}

std::string getip() {
    struct ifreq temp;
    struct sockaddr_in* addr_;
    int fd = 0;
    int ret = -1;
#ifdef OS_CENTOS6
    strcpy(temp.ifr_name, "eth0");
#else
    strcpy(temp.ifr_name, "ens33");
#endif
    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        return "";
    }
    ret = ioctl(fd, SIOCGIFADDR, &temp);
    close(fd);
    if (ret < 0) return "255.255.255.255";
    addr_ = (struct sockaddr_in*)&(temp.ifr_addr);
    std::string str_addr = std::string(inet_ntoa(addr_->sin_addr));
    //printf("ip: %s\n", str_addr.c_str());
    return str_addr;
}

nvmlReturn_t get_gpuoccupy(GPU_OCCUPY* g) {
    nvmlReturn_t result = NVML_ERROR_UNKNOWN;

    do {
        for (int j = 0; j < GPUNUM; ++j) {
            g->gpu[j] = -1;
            g->mem[j] = -1;
        }

#ifdef SUPPORT_GPU
        // First initialize NVML library
        result = nvmlInit();
        if (NVML_SUCCESS != result) {
            printf("Failed to initialize NVML: %s\n", nvmlErrorString(result));
            return result;
        }

        unsigned int device_count;
        result = nvmlDeviceGetCount(&device_count);
        if (NVML_SUCCESS != result) {
            printf("Failed to query device count: %s\n",
                   nvmlErrorString(result));
            break;
        }

        for (unsigned int i = 0; i < device_count; i++) {
            nvmlDevice_t device;
            char name[NVML_DEVICE_NAME_BUFFER_SIZE];
            nvmlPciInfo_t pci;

            result = nvmlDeviceGetHandleByIndex(i, &device);
            if (NVML_SUCCESS != result) {
                printf("Failed to get handle for device %i: %s\n", i,
                       nvmlErrorString(result));
                break;
            }

            result =
                nvmlDeviceGetName(device, name, NVML_DEVICE_NAME_BUFFER_SIZE);
            if (NVML_SUCCESS != result) {
                printf("Failed to get name of device %i: %s\n", i,
                       nvmlErrorString(result));
                break;
            }

            result = nvmlDeviceGetPciInfo(device, &pci);
            if (NVML_SUCCESS != result) {
                printf("Failed to get pci info for device %i: %s\n", i,
                       nvmlErrorString(result));
                break;
            }

            nvmlUtilization_t utilization_;
            result = nvmlDeviceGetUtilizationRates(device, &utilization_);
            if (result == NVML_SUCCESS) {
                g->gpu[i] = utilization_.gpu;
                g->mem[i] = utilization_.memory;
            }
        }
#endif
    } while (0);

#ifdef SUPPORT_GPU
    result = nvmlShutdown();
    if (NVML_SUCCESS != result)
        printf("Failed to shutdown NVML: %s\n", nvmlErrorString(result));
#endif
    return result;
}

void update_db(const char* localip, int cpu, const MEM_OCCUPY& mem,
               const DSK_OCCUPY& dsk, const GPU_OCCUPY& g) {
    try {
        sql::Driver* driver = NULL;
        sql::Connection* conn = NULL;
        sql::Statement* stmt = NULL;
        char sqlstmt[1024] = {0};
        do {
            driver = get_driver_instance();
            if (!driver) {
                break;
            }
            conn = driver->connect("tcp://192.168.1.15:3306", "defaultUser",
                                   "magician");
            if (!conn) {
                break;
            }
            conn->setSchema("seetaAtlas");
            stmt = conn->createStatement();
            if (!stmt) {
                break;
            }
            sprintf(sqlstmt,
                    "update dev_stat set cpu=%u, mem=%lu, "
                    "mem_free=%lu, storage=%llu, storage_free=%llu, "
                    "gpu0=%d, gpu1=%d, gpu2=%d, gpu3=%d, gpu4=%d, gpu5=%d, "
                    "gpu6=%d, gpu7=%d, "
                    "gpumem0=%d, gpumem1=%d, gpumem2=%d, gpumem3=%d, "
                    "gpumem4=%d, gpumem5=%d, gpumem6=%d, gpumem7=%d "
                    "where ip = '%s'",
                    cpu, mem.total_ / 1000, mem.free_ / 1000, dsk.dt_, dsk.df_,
                    g.gpu[0], g.gpu[1], g.gpu[2], g.gpu[3], g.gpu[4], g.gpu[5],
                    g.gpu[6], g.gpu[7], g.mem[0], g.mem[1], g.mem[2], g.mem[3],
                    g.mem[4], g.mem[5], g.mem[6], g.mem[7], localip);
            stmt->execute(sqlstmt);
            stmt->execute("commit");
            delete stmt;
            delete conn;
        } while (0);
    } catch (sql::SQLException& e) {
    }
}
