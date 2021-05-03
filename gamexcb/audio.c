/* audio.c: audio utils
 *
 * Copyright (C) 2020 by Aliaksei Katovich <aliaksei.katovich at gmail.com>
 *
 * All rights reserved.
 */

#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>

#define TAG "audio"

#include <rgu/log.h>
#include <rgu/audio.h>

#define sndcmd_ "ffplay -v 0 -nodisp -autoexit gamelib/src/jni/game/assets/%s >/dev/null"

static void *spawn(void *arg)
{
	struct track *track = arg;
	char cmd[sizeof(sndcmd_) + strlen(track->name)];
	snprintf(cmd, sizeof(cmd), sndcmd_, track->name);
	cmd[sizeof(cmd)] = '\0';
#if 0
	fprintf(stderr, "%s\n", cmd);
#endif
	system(cmd);
	return NULL;
}

static uint8_t findstr(const char *haystack, const char *needle, ssize_t len)
{
	const char *ptr = needle;
	size_t cnt = 0;
	size_t max = strlen(needle);

	while (--len > 0) {
		if (*haystack++ != *ptr) {
			cnt = 0;
			ptr = needle;
		} else {
			ptr++;
			if (++cnt == max)
				return 1;
		}
	}

	return 0;
}

static uint8_t is_playing(const char *name)
{
	DIR *dir = opendir("/proc");

	if (!dir)
		return 0;

	struct dirent *ent;
	char buf[256];

	while ((ent = readdir(dir))) {
		memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf), "/proc/%s/cmdline", ent->d_name);

		int fd;

		if ((fd = open(buf, O_RDONLY)) < 0)
			continue;

		memset(buf, 0, sizeof(buf));
		ssize_t len;
		if ((len = read(fd, buf, sizeof(buf))) < 0) {
			close(fd);
			continue;
		}
		close(fd);

		if (findstr(buf, name, len)) {
			ii("::: %s is playing :::\n", name);
			return 1;
		}
	}

	closedir(dir);
	return 0;
}

static void stop_track(const char *name)
{
	DIR *dir = opendir("/proc");

	if (!dir)
		return;

	struct dirent *ent;
	char buf[256];

	while ((ent = readdir(dir))) {
		memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf), "/proc/%s/cmdline", ent->d_name);

		int fd;

		if ((fd = open(buf, O_RDONLY)) < 0)
			continue;

		memset(buf, 0, sizeof(buf));
		ssize_t len;
		if ((len = read(fd, buf, sizeof(buf))) < 0) {
			close(fd);
			continue;
		}
		close(fd);

		if (findstr(buf, name, len)) {
			memset(buf, 0, sizeof(buf));
			snprintf(buf, sizeof(buf), "kill -9 %s", ent->d_name);
			dd("stop %s | %s\n", name, buf);
			system(buf);
			break;
		}
	}

	closedir(dir);
}

void audio_unload(struct track *track)
{
}

void audio_load(void *amgr, struct track *track)
{
	track->loaded = 1;
}

void audio_loop(struct track *track, uint8_t loop)
{
}

void audio_pause(struct track *track)
{
}

uint8_t audio_fade(struct track *track)
{
	return 0;
}

void audio_play(struct track *track)
{
#ifdef NO_SOUND
	return;
#endif
	if (is_playing(track->name))
		return;

	pthread_t t;
	pthread_create(&t, NULL, spawn, (void *) track);
}

void audio_stop(struct track *track)
{
	dd("::: stop track %s :::\n", track->name);
	stop_track(track->name);
}

float audio_progress(struct track *track)
{
	return 1;
}

void audio_close(struct engine **engine)
{
}

struct engine {};
static struct engine dummy_engine_;

struct engine *audio_open(void)
{
	ii("audio interface is ready\n");
	return &dummy_engine_;
}
