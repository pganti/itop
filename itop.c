#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <dirent.h>
#include <stdbool.h>
#include <fcntl.h>
#include <asm/param.h>
#include <stdarg.h>

#define JIFFIES_TO_MICROSECONDS(x) (((x)*1e6)/Hertz)

#ifndef ARG_MAX
#define ARG_MAX sysconf(_SC_ARG_MAX)
#endif

#define ERROR(fmt,arg...) fprintf(stderr, "%s:%d: "fmt, __func__, __LINE__, ##arg)

#define MGROUP "224.0.0.251"
#define MPORT 6000

#define JSON_TEMPLATE "{"ts":%lu,"cluster":"%s","host":"%s","cpuusage":%f,"coreusage":[%s],"loadavg":%s,"meminfo":{%s},"procinfo":{%s},"vfs":{%s},"net":{%s}}"
#define PROC_TEMPLATE ""%s":{"pid":%d,"state":"%c","threads":%d,"cpu_pct":%s,"mem_pct": %s,"oom_score":%d},"
#define VFS_TEMPLATE ""inode_usage":%llu,"dir_usage":%llu,"fd_usage": %llu"
#define NET_TEMPLATE ""tx_bytes":%llu,"tx_errors":%llu,"rx_bytes": %llu,"rx_errors":%llu"

#define MAX_LENGTH 8192
#define HOSTNAMEMAX 100
#define MAX_CORES 8
#define CPU_STAT_COUNT 10 /* user, nice, system, idle, iowait, irq, softirq, steal, guest, gnice */


unsigned long Hertz=(unsigned long)HZ; 
unsigned Sysmem=0;
long Btime=0;

char loadavg_buffer[MAX_LENGTH];
char meminfo_buffer[MAX_LENGTH];
char procinfo_buffer[MAX_LENGTH];
float core_usage[MAX_CORES+1];
int path_collect_list(const char *path, ...)
{
    int rc = 0;
    FILE *file = NULL;
    char file_buf[4096];
    va_list dest_list;
    va_start(dest_list, path);
    file = fopen(path, "r");
    if (file == NULL) {
        ERROR("cannot open `%s': %mn", path);
        rc = -1;
        goto out;
    }
    setvbuf(file, file_buf, _IOFBF, sizeof(file_buf));
    unsigned long long *dest;
    while ((dest = va_arg(dest_list, unsigned long long *)) != NULL) {
        if (fscanf(file, "%llu", dest) != 1) {
            ERROR("%s: no valuen", path);
            goto out;
        }
        rc++;
    }
    out:
    if (file != NULL)
    fclose(file);
    va_end(dest_list);
    return rc;
}
/*
* Get load average
*/
char *get_loadavg(void) {
    FILE *fp;
    char line[MAX_LENGTH];
    char dummy[16];
    double load[3];
    int i;
    fp = fopen("/proc/loadavg", "r");
    if (fp != NULL) {
        fgets(line, MAX_LENGTH, fp);
        load[0] = strtod(strtok(line, " n"), NULL);
        for(i=1;i<3;i++)
        load[i] = strtod(strtok(NULL, " n"), NULL);
        sprintf(loadavg_buffer, "[%f,%f,%f]", load[0],load[1],load[2]);
    }
    fclose(fp);
    return loadavg_buffer;
}
/*
* Get CPU usage by measuring jiffie counters - returns number of cores sampled
*/
int get_cpuusage(int interval) {
    FILE *fp = fopen("/proc/stat", "r");
    char line[MAX_LENGTH];
    long stat[CPU_STAT_COUNT*(MAX_CORES+1)] = {0};
    long delta[CPU_STAT_COUNT*(MAX_CORES+1)] = {0};
    int i, j, c, sum, offset = 0;
    float usage;
    char *word;
    if (NULL == fp) {
        fprintf(stderr, "Could not open '/proc/stat'n");
        exit(1);
    }
    /* First, sample current jiffie counters */
    if (!feof (fp)) {
        for(c=0;c<=MAX_CORES;c++) {
            if (NULL == fgets(line, MAX_LENGTH, fp)) {
                fclose(fp);
                fprintf(stderr, "Could not parse '/proc/stat'n");
                exit(1);
            }
            word = strtok(line, " n");
            if(strncmp(word, "cpu", 3) == 0) {
                offset = c * CPU_STAT_COUNT;
                for(i=0;i<CPU_STAT_COUNT;i++)
                stat[i+offset] = strtol(strtok(NULL, " n"), NULL, 10);
            }
            else break;
        }
    }
    fclose(fp);
    /* Now give the kernel time to update them */
    sleep(interval);
    /* And resample */
    fp = fopen("/proc/stat", "r");
    if (!fp) { printf("Cannot Open /proc/stat filen"); return 1;}
    if (!feof (fp)) {
        for(c=0;c<=MAX_CORES;c++) {
            fgets(line, MAX_LENGTH, fp);
            word = strtok(line, " n");
            if(word == NULL) continue;
            if(strncmp(word, "cpu", 3) == 0) {
                offset = c * CPU_STAT_COUNT;
                for(i=0;i<CPU_STAT_COUNT;i++)
                delta[i+offset] = strtol(strtok(NULL, " n"), NULL, 10);
            }
            else break;
        }
    }
    fclose(fp);
    /* Now compute the deltas and total time spent in each state */
    for(j=0;j<c;j++) {
        sum = 0;
        offset = j * CPU_STAT_COUNT;
        for(i=0;i<7;i++) {
            delta[i+offset] -= stat[i+offset];
            sum += delta[i+offset];
        }
        if(sum > 0) { /* /************************************************************\
            *
            \************************************************************/
            function of idle time */
            core_usage[j] = 1.0 - (delta[offset+3] / (1.0 * sum));
        }
        else core_usage[j] = 0.0;
    }
    return c;
}
/*
* Get ram and swap info
*/
char *get_meminfo(void) {
    FILE *fp;
    char unit[2];
    char label[128];
    char buffer[128];
    char *colon;
    int value;
    bzero(&meminfo_buffer, MAX_LENGTH);
    fp = fopen("/proc/meminfo", "r");
    if (fp != NULL) {
        while (!feof(fp)) {
            // fscanf is ugly, but serves us well here
            if(fscanf(fp, "%s %d %2s", label, &value, unit) != 3)
            break;
            colon = strchr(label,':');
            if(colon != NULL)
            *colon = '';
            if(!strncmp(unit, "kB", 2)) {
                sprintf(buffer, ""%s":%d,", label, value);
                strcat(meminfo_buffer, buffer);
            }
        }
    }
    fclose(fp);
    value = strlen(meminfo_buffer);
    if(value) {
        meminfo_buffer[value-1] = ''; /* strip comma */
    }
    return meminfo_buffer;
}
char *dump_proc_stats(void) {
    struct dirent *de;
    int value;
    char buffer[MAX_LENGTH];
    DIR *d = opendir("/proc");
    if (!d) {
        perror("opendire /proc");
        return NULL;
    }
    const long kb_per_page = sysconf(_SC_PAGESIZE) / 1024;
    char cbuf[1024];
    FILE* gg;
    gg=fopen( "/proc/stat", "r" );
    while(!feof(gg)) {
        if(fscanf(gg,"btime %ld", &Btime) == 1)
        break;
        if (fgets(cbuf, 1024, gg) == NULL)
        break;
    }
    fclose(gg);
    if( (gg = fopen( "/proc/meminfo", "r" )) != NULL ) {
        while(!feof(gg)) {
            if(fscanf(gg,"MemTotal: %u", &Sysmem) == 1){
                Sysmem *= 1024; /* Convert Sysmem from kB to bytes */
                Sysmem /= getpagesize(); /* Convert Sysmem from bytes to pages */
                break;
            }
            if (fgets(cbuf, 1024, gg) == NULL)
            break;
        }
        fclose(gg);
    }
    bzero(&procinfo_buffer, MAX_LENGTH);
    while ((de = readdir(d))!= NULL) {
        if (!isdigit(de->d_name[0]))
        continue;
        int pid = atoi(de->d_name);
        char cmndline[ARG_MAX];
        char fpath[PATH_MAX];
        FILE *fp,*f;
        /* Read full process path / package from cmdline. */
        sprintf(fpath, "/proc/%d/cmdline", pid);
        cmndline[0]=''; // initialize
        if( (fp = fopen( fpath, "r" )) != NULL ) {
            size_t got;
            if( (got = fread(cmndline, sizeof(char), ARG_MAX, fp)) < 1 ) {
                cmndline[ARG_MAX - 1] = '';
                } else {
                size_t i;
                for(i = 0; i < got; i++)
                if( cmndline[i] == '' )
                cmndline[i] = ' ';
                cmndline[got] = '';
            }
            fclose(fp);
        }
        if(strncmp(cmndline, "/opt", 4) !=0) continue;
        char state;
        int ppid;
        int pgrp;
        int session;
        int tty;
        int tpgid;
        unsigned flags;
        unsigned minflt;
        unsigned cminflt;
        unsigned majflt;
        unsigned cmajflt;
        long long utime;
        long long stime;
        long long cutime;
        long long cstime;
        int counter;
        int priority;
        unsigned timeout;
        unsigned itrealvalue;
        unsigned long starttime;
        unsigned long vsize;
        unsigned long rss;
        unsigned rlim;
        unsigned startcode;
        unsigned endcode;
        unsigned startstack;
        unsigned kstkesp;
        unsigned kstkeip;
        int signal;
        int blocked;
        int sigignore;
        int sigcatch;
        unsigned wchan;
        int result;
        unsigned long start;
        char proc_name[FILENAME_MAX];
        char display_name[64];
        char pctmem[32];
        char pctcpu[32];
        /* Read cpu/io/mem stats. */
        sprintf(fpath, "/proc/%d/stat", pid);
        f = fopen(fpath, "r");
        if (!f)
        continue;
        result = fscanf(fp,
        "%d %s %c %d %d %d %d %d %u %u %u %u %u %Ld %Ld %Ld %Ld %d %d %u %u %lu %lu %lu %u %u %u %u %u %u %d %d %d %d %u",
        &pid,
        proc_name,
        &state,
        &ppid,
        &pgrp,
        &session,
        &tty,
        &tpgid,
        &flags,
        &minflt,
        &cminflt,
        &majflt,
        &cmajflt,
        &utime,
        &stime,
        &cutime,
        &cstime,
        &counter,
        &priority,
        &timeout,
        &itrealvalue,
        &starttime,
        &vsize,
        &rss,
        &rlim,
        &startcode,
        &endcode,
        &startstack,
        &kstkesp,
        &kstkeip,
        &signal,
        &blocked,
        &sigignore,
        &sigcatch,
        &wchan);
        fclose(f);
        /* It's all or nothing baby */
        if (result != 35) return NULL;
        utime = JIFFIES_TO_MICROSECONDS(utime);
        stime = JIFFIES_TO_MICROSECONDS(stime);
        cutime = JIFFIES_TO_MICROSECONDS(cutime);
        cstime = JIFFIES_TO_MICROSECONDS(cstime);
        starttime = starttime/Hertz;
        timeout = JIFFIES_TO_MICROSECONDS(timeout);
        /* starttime is seconds since boot; convert to unix time */
        start = starttime + Btime;
        if(Sysmem != 0)
        sprintf(pctmem, "%3.2f", (float) (rss * 100 / Sysmem));
        sprintf(pctcpu, "%3.2f", (float)100 * ((utime + stime)/1e6)/(time(NULL)-start));
        unsigned long long oom_score = 0;
        sprintf(fpath, "/proc/%d/oom_score", pid);
        rc = path_collect_list(fpath,&oom_score, NULL);
        sprintf(buffer,PROC_TEMPLATE,strtok(proc_name, "()"),pid,state,counter,pctcpu,pctmem,oom_score);
        strcat(procinfo_buffer,buffer);
    }
    closedir(d);
    value = strlen(procinfo_buffer);
    if(value) {
        procinfo_buffer[value-1] = '';
    }
    return procinfo_buffer;
}
int main(int argc, char *argv[]) {
    struct sockaddr_in addr;
    int  i, msg_len, addr_len, sock, count;
    char hn[HOSTNAMEMAX],host[HOSTNAMEMAX];
    char msg[MAX_LENGTH], usage[MAX_LENGTH], scratch[MAX_LENGTH],vfs_buf[HOSTNAMEMAX],net_buf[HOSTNAMEMAX];
    FILE *fp;
    long unsigned ts = 0;
    /* first get the host we are working on */
    fp = fopen("/proc/sys/kernel/hostname", "r");
    if (fp != NULL) {
        fgets(host, HOSTNAMEMAX, fp);
        strtok(host, " n");
        } else {
        host[strlen(host)-1] = '';
    }
    fclose(fp);
    if (argc < 2) {
        printf("please provide the id prefix as argv[1]n");
        exit(2);
    }
    unsigned long long dentry_alloc = 0, dentry_free = 0,file_use=0;
    unsigned long long inode_alloc = 0, inode_free = 0;
    int rc;
    rc = path_collect_list("/proc/sys/fs/dentry-state",
    &dentry_alloc, &dentry_free, NULL);
    rc = path_collect_list("/proc/sys/fs/file-nr", &file_use,NULL);
    rc = path_collect_list("/proc/sys/fs/inode-state",
    &inode_alloc, &inode_free, NULL);
    sprintf(vfs_buf,VFS_TEMPLATE,inode_alloc-inode_free,dentry_alloc-dentry_free,file_use);
    unsigned long long tx_bytes = 0, tx_errors = 0;
    unsigned long long rx_bytes = 0, rx_errors = 0;
    rc = path_collect_list("/sys/class/net/eth0/statistics/tx_bytes",
    &tx_bytes, NULL);
    rc = path_collect_list("/sys/class/net/eth0/statistics/tx_errors",
    &tx_errors, NULL);
    rc = path_collect_list("/sys/class/net/eth0/statistics/rx_bytes",
    &rx_bytes, NULL);
    rc = path_collect_list("/sys/class/net/eth0/statistics/rx_errors",
    &rx_errors, NULL);
    sprintf(net_buf,NET_TEMPLATE,tx_bytes,tx_errors,rx_bytes,rx_errors);
    /* set up socket */
    char *opt;
    opt = "eth0";
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, opt, strlen(opt));
    if (sock < 0) {
        perror("Could not create socket, exiting");
        exit(2);
    }
    bzero((char *)&addr, sizeof(addr));
    addr.sin_family  = AF_INET;
    addr.sin_addr.s_addr = inet_addr(MGROUP);
    addr.sin_port = htons(MPORT);
    addr_len  = sizeof(addr);
    while (1) {
        bzero((char *)&usage, MAX_LENGTH);
        count = get_cpuusage(5);
        for(i=1;i<count;i++) { // skip aggregate count
            sprintf(scratch, "%f,", core_usage[i]);
            strcat(usage, scratch);
        }
        i = strlen(usage);
        if(i) {
            usage[i-1] = ''; /* strip comma */
        }
        struct timeval tv;
        gettimeofday(&tv, NULL);
        ts = (unsigned long long)(tv.tv_sec) * 1000;
        msg_len = sprintf(msg, JSON_TEMPLATE,ts,argv[1],host,core_usage[0], usage, get_loadavg(), get_meminfo(),dump_proc_stats(),vfs_buf,net_buf);
        // note that we're not sending msg_len + 1 data to avoid sending the .
        count = sendto(sock, msg, msg_len, 0, (struct sockaddr *) &addr, addr_len);
        if (count < 0) {
            perror("Error sending message");
            //exit(1); we shouldn't die due to transient failures
        }
        sleep(12);
    }
}
