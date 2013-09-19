// Idee: create a 1440x365 of your system's uptime


/*
Name: PN Stat (my first daemon)
Author: Paul Nechifor <paul@nechifor.net>
Started: 19.02.2010
Version 0.01: 20.02.2010

TODO:
    - It's better two write the difference between the values for some of them.
    - Don't write the values every minute. Maybe every 30 minutes?
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

#define INTERFACE "wlan0" // might be eth0 on your system
#define FAN_FILE "/sys/class/hwmon/hwmon1/fan1_input"
#define BAT_FILE "BAT0"
#define TEMP_FILE "TZ00"
#define NEW_LOG_BEFORE 64 * 1024 // limit the log file to 64kiB
#define LOG_DIR "/var/log/pnstat"

/*********************************************************************************************************************** 
  The structures
 */

typedef struct
{
    int total;
    int free;
    int buffers;
    int cached;
} mem_t;

typedef struct
{
    double m1;
    double m5;
    double m15;
} load_t;

typedef struct
{
    unsigned rx_bytes;
    unsigned rx_packets;
    unsigned rx_errs;
    unsigned rx_drop;
    unsigned tx_bytes;
    unsigned tx_packets;
    unsigned tx_errs;
    unsigned tx_drop;
} net_t;

typedef struct 
{
    int last_full;
    int present;
} bat_t;

typedef struct
{
    unsigned boot_time;
    int last_pid;
} stat_t;

/*********************************************************************************************************************** 
  The get functions
 */

// Reads the name of the value from the file.
void name(FILE* f, char* buf)
{
	int c, n = 0;
	while ( (c = fgetc(f)) == ' ' )
		;
	buf[n++] = c;
	while ( (c = fgetc(f)) != ':' )
		buf[n++] = c;
	buf[n] = 0;
}

// Reads the values from /proc/meminfo.
void get_mem(mem_t* mem)
{
	FILE* f;
	char buf[128];

    mem->total = mem->free = mem->buffers = mem->cached = -1;

	if ( (f = fopen("/proc/meminfo", "r")) == NULL)
	{
		perror("/proc/meminfo");
		exit(EXIT_FAILURE);
	}

	while (mem->total < 0 || mem->free < 0 || mem->buffers < 0 || mem->cached < 0)
	{
		name(f, buf);

		if (!strcmp(buf, "MemTotal"))
			fscanf(f, "%u", &mem->total);
		else if (!strcmp(buf, "MemFree"))
			fscanf(f, "%u", &mem->free);
		else if (!strcmp(buf, "Buffers"))
			fscanf(f, "%u", &mem->buffers);
		else if (!strcmp(buf, "Cached"))
			fscanf(f, "%u", &mem->cached);

		fgets(buf, sizeof(buf), f); // Ignore the rest of the line.
	}
	fclose(f);
}

// Reads the three load numbers.
void get_load(load_t* load)
{
	FILE* f;

	if ( (f = fopen("/proc/loadavg", "r")) == NULL)
	{
		perror("/proc/loadavg");
		exit(EXIT_FAILURE);
	}
	fscanf(f, "%lf %lf %lf", &load->m1, &load->m5, &load->m15);
	fclose(f);
}

// Reads the values from /proc/net/dev.
void get_net(net_t* net)
{
	
	FILE* f;
	char buf[1024];

	if ( (f = fopen("/proc/net/dev", "r")) == NULL)
	{
		perror("/proc/net/dev");
		exit(EXIT_FAILURE);
	}

	// remove the two line header
	fgets(buf, sizeof(buf), f);
	fgets(buf, sizeof(buf), f);

    int found_it = 0;

	do
	{
	   	name(f, buf);
		if (!strcmp(buf, INTERFACE))
        {
            found_it = 1;
            break;
        }
	} while (fgets(buf, sizeof(buf), f));

	fscanf(f, "%u %u %u %u", &net->rx_bytes, &net->rx_packets, &net->rx_errs, &net->rx_drop);

    unsigned v;
	for (int i = 0; i < 4; i++)
		fscanf(f, "%u", &v);

	fscanf(f, "%u %u %u %u", &net->tx_bytes, &net->tx_packets, &net->tx_errs, &net->tx_drop);

	fclose(f);
}

void get_bat(bat_t* bat)
{
    FILE* f;
    char buf[128];
    char f_name[64] = "/proc/acpi/battery/";

    strcat(f_name, BAT_FILE);
    strcat(f_name, "/info");

    if ( (f = fopen(f_name, "r")) == NULL)
    {
        perror(f_name);
        exit(EXIT_FAILURE);
    }

    do
    {
        name(f, buf);
        if (!strcmp(buf, "last full capacity"))
        {
            fscanf(f, "%d", &bat->last_full);
            break;
        }
    } while (fgets(buf, sizeof(buf), f));
    
    fclose(f);

    f_name[strlen(f_name) - 5] = '\0'; // Delete "/info".
    strcat(f_name, "/state");

    if ( (f = fopen(f_name, "r")) == NULL)
    {
        perror(f_name);
        exit(EXIT_FAILURE);
    }

    do
    {
        name(f, buf);
        if (!strcmp(buf, "remaining capacity"))
        {
            fscanf(f, "%d", &bat->present);
            break;
        }
    } while (fgets(buf, sizeof(buf), f));
    
    fclose(f);
}

void get_proc_temp(int* proc_temp)
{
    FILE* f;
    char buf[128];
    char f_name[64] = "/proc/acpi/thermal_zone/";

    strcat(f_name, TEMP_FILE);
    strcat(f_name, "/temperature");

    if ( (f = fopen(f_name, "r")) == NULL)
    {
        perror(f_name);
        exit(EXIT_FAILURE);
    }

    name(f, buf);
    fscanf(f, "%d", proc_temp);
    fclose(f);
}

void get_fan_speed(int* fan_speed)
{
    FILE* f;
    
    if ( (f = fopen(FAN_FILE, "r")) == NULL)
    {
        perror(FAN_FILE);
        exit(EXIT_FAILURE);
    }
    fscanf(f, "%d", fan_speed);
    fclose(f);
}
void get_stat(stat_t* stat)
{
    FILE* f;

    if ( (f = fopen("/proc/stat", "r")) == NULL)
    {
        perror("/proc/stat");
        exit(EXIT_FAILURE);
    }

    stat->boot_time = 0;
    stat->last_pid = 0;
    char whole[2048];

    while (stat->boot_time == 0 || stat->last_pid)
    {
        fscanf(f, "%s ", whole);
        if (!strcmp(whole, "btime"))
            fscanf(f, "%u", &stat->boot_time);
        else if (!strcmp(whole, "processes"))
            fscanf(f, "%d", &stat->last_pid);
        fgets(whole, sizeof(whole), f);
    }
    fclose(f);
}

/*********************************************************************************************************************** 
  The global values
 */

FILE* log_file;
char** last_row;
int col_index;
char curr_line[1024];
int file_size;
int log_nr;


/*********************************************************************************************************************** 
  The logging functions
 */

void start_logging(int nr_cols, char names[][100])
{
    last_row = (char**) malloc(nr_cols * sizeof(char*));
    curr_line[0] = '\0';

    char lname[128];
    strcpy(lname, LOG_DIR);
    strcat(lname, "/log");

    if ( ( log_file = fopen(lname, "a")) == NULL)
    {
        perror(lname);
        exit(EXIT_FAILURE);
    }

    fputc('=', log_file);
    for (int i = 0; i < nr_cols; i++)
    {
        last_row[i] = (char*) malloc(256 * sizeof(char));
        strcpy(last_row[i], "an impossible value no field can have");

        fputs(names[i], log_file);
        if (i < nr_cols - 1)
            fputc(';', log_file);
    }
    fputc('\n', log_file);
    col_index = 0;

    // Get the file size
    int pos = ftell(log_file);
    fseek(log_file, 0, SEEK_END);
    file_size = ftell(log_file);
    fseek(log_file, pos, SEEK_SET);

    // Get the largest log number
    DIR* dir = opendir(LOG_DIR);
    if (dir == NULL)
    {
        perror(LOG_DIR);
        exit(EXIT_FAILURE);
    }

    log_nr = 0;
    struct dirent* entry;
    while (( entry = readdir(dir) ) != NULL)
    {
        char* n = entry->d_name;
        if (strlen(n) == 9 && n[0] == 'l' && n[1] == 'o' && n[2] == 'g') // "log000000"
        {
            int nr = atoi(n + 3);
            if (nr > log_nr) log_nr = nr;
        }
    }

    closedir(dir);
}
void add_line_value(char* value)
{
    if (strcmp(last_row[col_index], value))
    {
        strcat(curr_line, value);
        strcpy(last_row[col_index], value);
    }
    strcat(curr_line, ";");
    col_index++;
}
void end_line()
{
    for (int i = strlen(curr_line) - 1; i > 0; i--)
        if (curr_line[i] == ';')
            curr_line[i] = '\0';
        else
            break;

    strcat(curr_line, "\n");
    int len = strlen(curr_line);

    if (len + file_size > NEW_LOG_BEFORE) 
    {
        char new[128], old[128];
        sprintf(new, "%s/log%06d", LOG_DIR, log_nr);
        sprintf(old, "%s/log", LOG_DIR);

        fclose(log_file);
        rename(old, new);

        if ( (log_file = fopen(old, "a")) == NULL)
        {
            perror(old);
            exit(EXIT_FAILURE);
        }

        log_nr++;
        file_size = 0;
    }

    fputs(curr_line, log_file);
    //printf("%s", curr_line); // for debuging

    file_size += len;
    curr_line[0] = '\0';
    col_index = 0;
}

void sigint(int sig)
{
    fclose(log_file);
    exit(EXIT_SUCCESS);
}

void daemonize()
{
    pid_t pid = fork();
    if (pid == -1)
        exit(EXIT_FAILURE);

    if (pid > 0)
        exit(EXIT_SUCCESS);

    // Change the file mode mask
    umask(0);

    pid_t sid = setsid();
    if (sid == -1)
        exit(EXIT_FAILURE);

    if (chdir("/") == -1)
        exit(EXIT_FAILURE);

    freopen("/dev/null", "r", stdin);
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
}

void align()
{
    while (time(NULL) % 60 != 0)
        usleep(10000);
}

/*********************************************************************************************************************** 
  And last, but certanly not least, the main function
 */

int main()
{
    daemonize();

    mem_t mem;
    load_t load;
    net_t net;
    bat_t bat;
    int proc_temp;
    int fan_speed;
    stat_t stat;

    signal(SIGINT, sigint);
    signal(SIGUSR1, sigint);

    char col_names[][100] = 
    {
        "time",
        "fan_speed",
        "mem_free",
        "mem_buffers",
        "mem_cached",
        "rx_bytes",
        "rx_packets",
        "tx_bytes",
        "tx_packets",
        "last_pid",
        "load_m1",
        "load_m5",
        "load_m15",
        "proc_temp",
        "bat_present",
        "rx_errs",
        "rx_drop",
        "tx_errs",
        "tx_drop",
        "bat_last_full",
        "boot_time",
        "mem_total"
    };
    char buf[256];

    start_logging(22, col_names); // Modify this number
    align();

    for (;;)
    {
        get_mem(&mem);
        get_load(&load);
        get_net(&net);
        get_bat(&bat);
        get_proc_temp(&proc_temp);
        get_fan_speed(&fan_speed);
        get_stat(&stat);

        sprintf(buf, "%u", (unsigned) time(NULL)); add_line_value(buf);
        sprintf(buf, "%d", fan_speed); add_line_value(buf);
        sprintf(buf, "%d", mem.free); add_line_value(buf);
        sprintf(buf, "%d", mem.buffers); add_line_value(buf);
        sprintf(buf, "%d", mem.cached); add_line_value(buf);
        sprintf(buf, "%u", net.rx_bytes); add_line_value(buf);
        sprintf(buf, "%u", net.rx_packets); add_line_value(buf);
        sprintf(buf, "%u", net.tx_bytes); add_line_value(buf);
        sprintf(buf, "%u", net.tx_packets); add_line_value(buf);
        sprintf(buf, "%.2f", load.m1); add_line_value(buf);
        sprintf(buf, "%.2f", load.m5); add_line_value(buf);
        sprintf(buf, "%.2f", load.m15); add_line_value(buf);
        sprintf(buf, "%d", stat.last_pid); add_line_value(buf);
        sprintf(buf, "%d", proc_temp); add_line_value(buf);
        sprintf(buf, "%d", bat.present); add_line_value(buf);
        sprintf(buf, "%u", net.rx_errs); add_line_value(buf);
        sprintf(buf, "%u", net.rx_drop); add_line_value(buf);
        sprintf(buf, "%u", net.tx_errs); add_line_value(buf);
        sprintf(buf, "%u", net.tx_drop); add_line_value(buf);
        sprintf(buf, "%d", bat.last_full); add_line_value(buf);
        sprintf(buf, "%u", stat.boot_time); add_line_value(buf);
        sprintf(buf, "%d", mem.total); add_line_value(buf);
        end_line();

        if (sleep(60) != 0)
            align();
    }
}
