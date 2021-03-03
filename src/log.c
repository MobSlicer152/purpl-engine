#include "purpl/log.h"

#ifdef __cplusplus
extern "C" {
#endif

struct purpl_logger *purpl_init_logger(u8 *first_index_ret, s8 default_level,
				       s8 first_max_level,
				       const char *first_log_path, ...)
{
	struct purpl_logger *logger;
	char *first;
	va_list args;
	size_t len;
	u8 first_index;

	PURPL_RESET_ERRNO;

	/* First, check our parameters */
	if (!first_index_ret || !first_log_path) {
		errno = EINVAL;
		return NULL;
	}

	/* Allocate the structure */
	logger = PURPL_CALLOC(1, struct purpl_logger);
	if (!logger)
		return NULL;

	/* Format the path to the first log */
	va_start(args, first_log_path);
	first = purpl_fmt_text_va(&len, first_log_path, args);
	va_end(args);

	/* Allocate the file stream pointers */
	logger->logs = PURPL_CALLOC(PURPL_MAX_LOGS, FILE *);

	/* Open the first log and fill out the structure */
	logger->default_index = purpl_open_log(logger, first_max_level, first) &
				0xFFFFFF;
	if (logger->default_index < 0) {
		free(logger);
		return NULL;
	}

	logger->default_level =
		((default_level < 0) ? PURPL_INFO : default_level) & 0xFFF;

	/* Return the index of the first log\ */
	first_index = logger->default_index;
	*first_index_ret = first_index;

	PURPL_RESET_ERRNO;

	return logger;
}

int purpl_open_log(struct purpl_logger *logger, s8 max_level, const char *path,
		   ...)
{
	u8 index;
	char *fmt_path;
	size_t len;
	va_list args;

	PURPL_RESET_ERRNO;

	/* Check the parameters */
	if (!logger || !path) {
		errno = EINVAL;
		return -1;
	}

	/* Format the path to the log */
	va_start(args, path);
	fmt_path = purpl_fmt_text_va(&len, path, args);
	va_end(args);

	/* Determine the index */
	index = logger->nlogs & 0xFFFFFF;

	/* Open the log */
	if (strcmp(fmt_path, "stdout") == 0) {
		logger->logs[index] = stdout;
	} else if (strcmp(fmt_path, "stderr") == 0) {
		logger->logs[index] = stderr;
	} else {
		logger->logs[index] = fopen(fmt_path, PURPL_OVERWRITE);
		if (logger->logs[index] == NULL)
			return -1;
	}

	/* Set the max level for the log */
	logger->max_level[index] = (max_level < 0) ? PURPL_DEBUG : max_level;

	PURPL_RESET_ERRNO;

	/* Increment the number of logs and return */
	logger->nlogs++;
	return index &
	       0xFFFFFF; /* Gotta mask off any extra for the bit field */
}

/* Some macros for the text of different message levels */
#define PRE_WTF "[???] "
#define PRE_FATAL "[fatal] "
#define PRE_ERROR "[error] "
#define PRE_WARNING "[warning] "
#define PRE_INFO "[info] "
#define PRE_DEBUG "[debug] "

size_t purpl_write_log(struct purpl_logger *logger, const char *file,
		       const int line, s8 index, s8 level, const char *fmt, ...)
{
	time_t rawtime;
	struct tm *now;
	char *fmt_ptr;
	char *lvl_pre;
	char *msg;
	u8 idx;
	u8 lvl;
	size_t fmt_len;
	size_t msg_len;
	size_t written;
	FILE *fp;
	va_list args;

	PURPL_RESET_ERRNO;

	/* Check our args */
	if (!logger || !file || !line || !fmt) {
		errno = EINVAL;
		return -1;
	}

	/* Format fmt */
	va_start(args, fmt);
	fmt_ptr = purpl_fmt_text_va(&fmt_len, fmt, args);
	va_end(args);

	/* Evaluate what level to use */
	lvl = (level < 0) ? logger->default_level : level;
	switch (lvl) {
	case PURPL_WTF:
		lvl_pre = PURPL_CALLOC(strlen(PRE_WTF) + 1, char);
		if (!lvl_pre) {
			(!fmt_len) ? (void)0 : free(fmt_ptr);
			return -1;
		}

		strcpy(lvl_pre, PRE_WTF);
		break;
	case PURPL_FATAL:
		lvl_pre = PURPL_CALLOC(strlen(PRE_FATAL) + 1, char);
		if (!lvl_pre) {
			(!fmt_len) ? (void)0 : free(fmt_ptr);
			return -1;
		}

		strcpy(lvl_pre, PRE_FATAL);
		break;
	case PURPL_ERROR:
		lvl_pre = PURPL_CALLOC(strlen(PRE_ERROR) + 1, char);
		if (!lvl_pre) {
			(!fmt_len) ? (void)0 : free(fmt_ptr);
			return -1;
		}

		strcpy(lvl_pre, PRE_ERROR);
		break;
	case PURPL_WARNING:
		lvl_pre = PURPL_CALLOC(strlen(PRE_WARNING) + 1, char);
		if (!lvl_pre) {
			(!fmt_len) ? (void)0 : free(fmt_ptr);
			return -1;
		}

		strcpy(lvl_pre, PRE_WARNING);
		break;
	case PURPL_INFO:
		lvl_pre = PURPL_CALLOC(strlen(PRE_INFO) + 1, char);
		if (!lvl_pre) {
			(!fmt_len) ? (void)0 : free(fmt_ptr);
			return -1;
		}

		strcpy(lvl_pre, PRE_INFO);
		break;
	case PURPL_DEBUG:
		lvl_pre = PURPL_CALLOC(strlen(PRE_DEBUG) + 1, char);
		if (!lvl_pre) {
			(!fmt_len) ? (void)0 : free(fmt_ptr);
			return -1;
		}

		strcpy(lvl_pre, PRE_DEBUG);
		break;
	default:
		lvl_pre = PURPL_CALLOC(strlen(PRE_WTF) + 1, char);
		if (!lvl_pre) {
			(!fmt_len) ? (void)0 : free(fmt_ptr);
			return -1;
		}

		strcpy(lvl_pre, PRE_WTF);
		break;
	}

	/* Get the time */
	time(&rawtime);
	now = localtime(&rawtime);

	/* Figure out our index */
	idx = (index < 0) ? logger->default_index : index;
	fp = logger->logs[idx];

	/* Format the message */
	msg = purpl_fmt_text(&msg_len, "%s%s:%d %d:%d:%d %d/%d/%d: %s\n",
			     lvl_pre, file, line, now->tm_hour, now->tm_min,
			     now->tm_sec, now->tm_mday, now->tm_mon + 1,
			     now->tm_year - 70, fmt_ptr);
	if (!msg) {
		(fmt_len) ? (void)0 : free(fmt_ptr);
		free(lvl_pre);
		return -1;
	}

	/* Write the message */
	written = fwrite(msg, sizeof(char), msg_len - 1, fp);
	if (!written) {
		(fmt_len) ? (void)0 : free(fmt_ptr);
		free(lvl_pre);
		return -1;
	}

	/* Just in case */
	fflush(fp);

	/* Free everything else */
	(msg_len) ? (void)0 : free(msg);
	(fmt_len) ? (void)0 : free(fmt_ptr);
	free(lvl_pre);

	PURPL_RESET_ERRNO;

	return written;
}

s8 purpl_set_max_level(struct purpl_logger *logger, u8 index, u8 level)
{
	u8 idx = index & 0xFFFFFF;

	PURPL_RESET_ERRNO;

	/* Check arguments */
	if (!logger || idx > logger->nlogs) {
		errno = EINVAL;
		return -1;
	}

	/* Change the level */
	logger->max_level[idx] = level & 0xFFF;

	PURPL_RESET_ERRNO;

	return level;
}

void purpl_close_log(struct purpl_logger *logger, u8 index)
{
	PURPL_RESET_ERRNO;

	/* Check our args */
	if (!logger || !logger->logs[index]) {
		errno = EINVAL;
		return;
	}

	/* Close the file and clear its information */
	fclose(logger->logs[index]);
	logger->nlogs--;
	logger->max_level[index] = 0;

	PURPL_RESET_ERRNO;
}

void purpl_end_logger(struct purpl_logger *logger, _Bool write_goodbye)
{
	uint i;

	PURPL_RESET_ERRNO;

	/* Check args */
	if (!logger) {
		errno = EINVAL;
		return;
	}

	/* Close every log */
	for (i = 0; i < logger->nlogs; i++) {
		/* Write a goodbye message if requested */
		if (write_goodbye) {
			char goodbye[512];
			time_t now_raw;
			struct tm *now;

			/* Copy in the first part of the message */
			strcpy(goodbye,
			       "This logger instance is terminating. Have a ");

			/* Now choose an adjective */
			if (rand() % 10 < 5)
				strcat(goodbye, "nice ");
			else
				strcat(goodbye, "good ");

			/* Append the right time of day */
			time(&now_raw);
			now = localtime(&now_raw);
			if (!now) /* time() failed */
				strcat(goodbye, "day.");
			else if (now->tm_hour < 12)
				strcat(goodbye, "morning.");
			else if (now->tm_hour >= 12 && now->tm_hour < 17)
				strcat(goodbye, "afternoon.");
			else if (now->tm_hour >= 17 && now->tm_hour < 18)
				strcat(goodbye, "evening.");
			else if (now->tm_hour >= 18)
				strcat(goodbye, "night.");

			purpl_write_log(logger, FILENAME, __LINE__, i,
					logger->max_level[i], "%s", goodbye);
		}

		/* Close the log */
		purpl_close_log(logger, i);
	}

	/* Free the logger */
	free(logger);

	PURPL_RESET_ERRNO;
}

#ifdef __cplusplus
}
#endif
