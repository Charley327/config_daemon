#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#define _GNU_SOURCE
#include <getopt.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#define QDEBUG_PRINTF(x, args...)  if (g_debug) {printf("%s:%d::"x, __FUNCTION__, __LINE__, ##args); }

typedef struct {
	char *qpr_item;
	char *qpr_value;
} QPAIR;

typedef struct {
	int qsec_pair_total_count;
	int qsec_pair_used_count;
	char *qsec_name;
	QPAIR *qsec_pair;
	QPAIR *qsec_cur_pair;
} QSECTION;

typedef struct {
	int qcfg_section_total_count;
	int qcfg_section_used_count;
	QSECTION *qcfg_section;
	QSECTION *qcfg_cur_section;
} QCONFIG;

int g_section_unit_count = 64;
int g_pair_unit_count = 64;
int g_refresh_time = 60;
char *g_config_file = "/etc/default_config/uLinux.conf";
int g_debug = 0;
int g_auto_test = 0;
int g_gen_auto_test = 0;

int QConfigSet(QCONFIG *qconfig, const char *section, const char *item, const char *value);
int QConfigGet(QCONFIG *qconfig, const char *section, const char *item, char *buffer, int buffer_size);
static char *normalize_line(char *line);
int init_qconfig(QCONFIG *qconfig);
int release_qconfig(QCONFIG *qconfig);

static int auto_test(QCONFIG *qconfig, const char *autotest_file)
{
	int ret, line_no=0;
	char buffer[4096];

	if ((ret = init_qconfig(qconfig)) != 0) {
		fprintf(stderr, "Test failed as init_qconfig. error %d\n", ret);
		return ret;
	}
	FILE *fp = fopen(autotest_file, "r");
	if (fp == NULL) {
		release_qconfig(qconfig);
		return -errno;
	}
	while (1) {
		if (fgets(buffer, sizeof(buffer), fp) == NULL)
			break;
		normalize_line(buffer);
		line_no++;
		char *token[3], *str;
		char *file, *section, *item, *exp_val;
		int i;
		char value[1024];
		for (i=0, str=buffer; i<3; i++, str=NULL) {
			if ((token[i] = strtok_r(str, ":", &exp_val)) == NULL) {
				fprintf(stderr, "token error at line %d, #%d\n", line_no, i);
				release_qconfig(qconfig);
				exit(1);
			}
		}
		file = token[0];
		section = token[1];
		item = token[2];
		if ((ret = QConfigGet(qconfig, section, item, value, sizeof(value))) < 0) {
			fprintf(stderr, "Test failed at line %d. QConfigGet failed(%d)\n", line_no, ret);
			continue;
		}
		if (strcmp(value, exp_val) != 0) {
			fprintf(stderr, "Test failed at line %d. Not expext value(%s, %s)\n", line_no, value, exp_val);
			break;
		}
	}
	fclose(fp);
	release_qconfig(qconfig);
	return ret;
}
static int gen_auto_test(QCONFIG *qconfig)
{
	char cmd[4096];
	int s, p;
	QSECTION *section = qconfig->qcfg_section;
	for (s=0; s<qconfig->qcfg_section_used_count; s++, section ++) {
		QPAIR *pair = section->qsec_pair;
		for (p=0; p<section->qsec_pair_used_count; p++, pair ++) {
			snprintf(cmd, sizeof(cmd), "echo %s:%s:%s:`getcfg '%s' '%s' -f '%s'`", g_config_file, section->qsec_name, pair->qpr_item, section->qsec_name, pair->qpr_item, g_config_file);
			system(cmd);
//			printf("%s=%s\n", pair->qpr_item, pair->qpr_value);
		}
	}
	return 0;
}

static char *normalize_line(char *line)
{
	if (line == NULL)
		return NULL;
	int len = strlen(line);
	if (len <= 0)
		return NULL;
	if (line[len-1] == '\n')
		line[-- len] = 0;
	char *nline = line;
	for (nline = line; *nline == ' '||*nline == '\t'; nline ++) {
	}
	while (-- len >= 0) {
		if (line[len] != ' ' && line[len] != '\t') {
			break;
		}
		line[len] = 0;
	}
	return nline;
}
int release_qconfig(QCONFIG *qconfig)
{
	int s, p;
	QSECTION *section = qconfig->qcfg_section;
	for (s=0; s<qconfig->qcfg_section_used_count; s++, section ++) {
		QPAIR *pair = section->qsec_pair;
		for (p=0; p<section->qsec_pair_used_count; p++, pair ++) {
			free(pair->qpr_item);
			free(pair->qpr_value);
		}
		free(section->qsec_pair);
	}
	free(qconfig->qcfg_section);
	return 0;
}
int init_qconfig(QCONFIG *qconfig)
{
	if (!g_config_file) {
		return -EINVAL;
	}
	if ((qconfig->qcfg_section = (QSECTION *)calloc(g_section_unit_count, sizeof(QSECTION))) == NULL) {
		return -ENOMEM;
	}
	qconfig->qcfg_cur_section = NULL;
	qconfig->qcfg_section_total_count = g_section_unit_count;
	qconfig->qcfg_section_used_count = 0;

	char buffer[4096];
	FILE *fp = fopen(g_config_file, "r");
	if (fp == NULL) {
		return -ENOENT;
	}
	char *line;
	char *section = NULL;
	while (1) {
		if (fgets(buffer, sizeof(buffer), fp) == NULL)
			break;
QDEBUG_PRINTF("line:%s", buffer);
		if ((line = normalize_line(buffer)) == NULL)
			continue;
QDEBUG_PRINTF("nline:<%s>\n", line);
		if (*line == '[') {	//this is a section
			char *end = strchr(line, ']');
			if (end == NULL)
				continue;
			*end = 0;
			if (section) {
				free(section);
			}
			section = strdup(line+1);
		}
		else {
			char *value;
			if ((value = strchr(line, '=')) == NULL) {
				continue;
			}
			*value = 0;
			value ++;

			char *item = normalize_line(line);
			value = normalize_line(value);
QDEBUG_PRINTF("QConfigSet(%s, %s, %s)\n", section, item, value);
			QConfigSet(qconfig, section, item, value);
		}
	}
	if (section)
		free(section);
	fclose(fp);
	return 0;
}
QPAIR *add_section_pair(QSECTION *section, const char *item)
{
	QPAIR *new_pair = NULL;
	int new_count = section->qsec_pair_total_count;
	if (section->qsec_pair_used_count >= section->qsec_pair_total_count) {
		new_count += g_pair_unit_count;
		new_pair = calloc(new_count, sizeof(QPAIR));
		if (new_pair == NULL) {
			return NULL;
		}
	}
	int i;
	QPAIR *old_pair = section->qsec_pair;
	for (i=0; i<section->qsec_pair_used_count; i++) {
		if (strcmp(old_pair[i].qpr_item, item) > 0) {
			break;
		}
	}
	char *new_name = strdup(item);
	if (new_name == NULL) {
		if (new_pair) free(new_pair);
		return NULL;
	}
	if (new_pair == NULL) {
		new_pair = old_pair;
	}
	else {
		if (old_pair)
			memmove(new_pair, old_pair, sizeof(QPAIR)*i);
	}
	if (old_pair)
		memmove(new_pair+(i+1), old_pair+i, sizeof(QPAIR)*(section->qsec_pair_total_count-i-1));
	new_pair[i].qpr_item = new_name;
	new_pair[i].qpr_value = NULL;
	if (old_pair != new_pair) {
		free(old_pair);
	}

	section->qsec_pair = new_pair;
	section->qsec_cur_pair = new_pair+i;
	section->qsec_pair_used_count ++;
	section->qsec_pair_total_count = new_count;
	return section->qsec_cur_pair;
}
static int pair_comp(const void *m1, const void *m2)
{
	QPAIR *p1 = (QPAIR *)m1;
	QPAIR *p2 = (QPAIR *)m2;
	return strcmp(p1->qpr_item, p2->qpr_item);
}
QPAIR *get_section_pair(QSECTION *section, const char *item)
{
	if (section->qsec_cur_pair && strcmp(section->qsec_cur_pair->qpr_item, item) == 0) {
		return section->qsec_cur_pair;
	}
	QPAIR pair, *match_pair;
	pair.qpr_item = (char *)item;
	if (section->qsec_pair == NULL)
		return NULL;
	if ((match_pair = bsearch(&pair, section->qsec_pair, section->qsec_pair_used_count, sizeof(QPAIR), pair_comp)) == NULL) {
		return NULL;
	}
	section->qsec_cur_pair = match_pair;
	return match_pair;
}
static int section_comp(const void *m1, const void *m2)
{
	QSECTION *s1 = (QSECTION *)m1;
	QSECTION *s2 = (QSECTION *)m2;
	return strcmp(s1->qsec_name, s2->qsec_name);
}
QSECTION *get_config_section(QCONFIG *qconfig, const char *section)
{
	if (qconfig->qcfg_cur_section && strcmp(qconfig->qcfg_cur_section->qsec_name, section) == 0) {
		return qconfig->qcfg_cur_section;
	}
	QSECTION sec, *match_sec;
	sec.qsec_name = (char *)section;
	if (qconfig->qcfg_section == NULL)
		return NULL;
	if ((match_sec = bsearch(&sec, qconfig->qcfg_section, qconfig->qcfg_section_used_count, sizeof(QSECTION), section_comp)) == NULL) {
		return NULL;
	}
	qconfig->qcfg_cur_section = match_sec;
	return match_sec;
}
QSECTION *add_config_section(QCONFIG *qconfig, const char *section)
{
	QSECTION *new_section = NULL;
	int new_count = qconfig->qcfg_section_total_count;
	if (qconfig->qcfg_section_used_count >= qconfig->qcfg_section_total_count) {
		new_count += g_section_unit_count;
		new_section = calloc(new_count, sizeof(QSECTION));
		if (new_section == NULL) {
			return NULL;
		}
	}
	int i;
	QSECTION *old_section = qconfig->qcfg_section;
	for (i=0; i<qconfig->qcfg_section_used_count; i++) {
		if (strcmp(old_section[i].qsec_name, section) > 0) {
			break;
		}
	}
	char *new_name;
	
	if (section)
		new_name = strdup(section);
	else
		new_name = strdup("");
	if (new_name == NULL) {
		if (new_section) free(new_section);
		return NULL;
	}
	if (new_section == NULL) {
		new_section = old_section;
	}
	else {
		memmove(new_section, old_section, sizeof(QSECTION)*i);
	}
	if (old_section)
		memmove(new_section+(i+1), old_section+i, sizeof(QSECTION)*(qconfig->qcfg_section_total_count-i-1));
	new_section[i].qsec_name = new_name;
	new_section[i].qsec_pair_total_count = 0;
	new_section[i].qsec_pair_used_count = 0;
	new_section[i].qsec_pair = NULL;
	new_section[i].qsec_cur_pair = NULL;
	if (old_section != new_section) {
		free(old_section);
	}
	qconfig->qcfg_section = new_section;
	qconfig->qcfg_cur_section = new_section+i;
	qconfig->qcfg_section_used_count ++;
	qconfig->qcfg_section_total_count = new_count;
	return qconfig->qcfg_cur_section;
}
int QConfigSet(QCONFIG *qconfig, const char *section, const char *item, const char *value)
{
	QSECTION *cur_sec = NULL;
	if ((cur_sec = get_config_section(qconfig, section)) == NULL) {
		if ((cur_sec = add_config_section(qconfig, section)) == NULL) {
			return -ENOMEM;
		}
	}

	QPAIR *cur_pair = get_section_pair(cur_sec, item);
	if (cur_pair == NULL) {
		if ((cur_pair = add_section_pair(cur_sec, item)) == NULL) {
			return -ENOMEM;
		}
QDEBUG_PRINTF("add_section_pair:%p, %s, %s\n", cur_pair, cur_pair->qpr_item, cur_pair->qpr_value);
	}
	else
QDEBUG_PRINTF("get_section_pair:%p, %s, %s\n", cur_pair, cur_pair->qpr_item, cur_pair->qpr_value);

	if (cur_pair->qpr_value) {
		if (strcmp(cur_pair->qpr_value, value) == 0)
			return 0;
QDEBUG_PRINTF("DEBUG:%d:free(%s, %s, %s, %p)\n", __LINE__, cur_sec->qsec_name, cur_pair->qpr_item, cur_pair->qpr_value, cur_pair->qpr_value);
		free(cur_pair->qpr_value);
	}
	if (value == NULL)
		value = "";
	cur_pair->qpr_value = strdup(value);
QDEBUG_PRINTF("DEBUG:%d:cur_pair->qpr_value=%s, %p\n", __LINE__, cur_pair->qpr_value, cur_pair->qpr_value);
	return 0;
}
int QConfigGet(QCONFIG *qconfig, const char *section, const char *item, char *buffer, int buffer_size)
{
	QSECTION *cur_sec = NULL;
	if (qconfig->qcfg_cur_section && strcmp(qconfig->qcfg_cur_section->qsec_name, section) == 0) {
		cur_sec = qconfig->qcfg_cur_section;
	}
	if (!cur_sec) {
		if ((cur_sec = get_config_section(qconfig, section)) == NULL) {
			return -ENOENT;
		}
	}

	QPAIR *cur_pair = get_section_pair(cur_sec, item);
	if (cur_pair == NULL) {
		return -ENOENT;
	}

	if (!cur_pair->qpr_value) {
		return -ENODATA;
	}
	snprintf(buffer, buffer_size, "%s", cur_pair->qpr_value);
	int len = strlen(cur_pair->qpr_value);
	if (len < buffer_size)
		return 0;
	return len+1;
}

int main(int argc, char *argv[])
{
/*
	int i;
	int count;
	char *fifo_path="/var/proftpd/fifo";
	char *rootpath = NULL;
*/
	int opt;
	char *autotest_file = NULL;

	while ((opt = getopt(argc,argv,"s:p:t:f:T:DG")) != -1) {
		switch (opt) {
		case 's':
			g_section_unit_count = atoi(optarg);
			break;
		case 'p':
			g_pair_unit_count = atoi(optarg);
			break;
		case 'r':
			g_refresh_time = atoi(optarg);
			break;
		case 'f':
			g_config_file = optarg;
			break;
		case 'D':
			g_debug = 1;
			break;
		case 'T':
			g_auto_test = 1;
			autotest_file = optarg;
			break;
		case 'G':
			g_gen_auto_test = 1;
			break;
		}
	}
	QCONFIG qconfig;
	int ret;
	if (g_auto_test) {
		return auto_test(&qconfig, autotest_file);
	}
	else if (g_gen_auto_test) {
		ret = init_qconfig(&qconfig);
		if (ret == 0) {
			gen_auto_test(&qconfig);
			release_qconfig(&qconfig);
		}
	}
	else {
		ret = init_qconfig(&qconfig);
		printf("QCONFIG return %d\n", ret);
		release_qconfig(&qconfig);
	}
/*
	if (rootpath) {
		int ret = chroot(rootpath);
		if (ret == -1) {
			printf("chroot(%s) failed(%d)\n", rootpath, errno);
			return 1;
		}
		chdir("/");
		getchar();
	}
	for (i = 0; i < count; i ++) {
		CreateFIFO(i, "cmdwrite", fifo_path);
		CreateFIFO(i, "cmdread", fifo_path);
		CreateFIFO(i, "rpcwrite", fifo_path);
		CreateFIFO(i, "rpcread", fifo_path);
	}
*/
	return 0;
}
/*
int CreateFIFO(int i, const char *name, const char *path)
{
	char fifo_file[1024];
	snprintf(fifo_file, sizeof(fifo_file), "%s/%s-%d", path, name, i);
	int ret = mkfifo(fifo_file, 0666);
	printf("mkfifo(%s) return %d(%d)\n", fifo_file, ret, errno);
	return ret;
}
*/
